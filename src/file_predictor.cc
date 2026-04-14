#include "src/file_predictor.h"

extern "C" {
#include <dirent.h>
#include <libgen.h>
#include <sys/types.h>
}

#include <filesystem>
#include <functional>
#include <regex>
#include <set>
#include <variant>
#include <vector>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/futures/delete_notification.h"
#include "src/infrastructure/dirname.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/error/view.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/text/sorted_line_sequence.h"
#include "src/language/wstring.h"
#include "src/predictor.h"
#include "src/structure.h"
#include "src/tests/tests.h"
#include "src/vm/escape.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::concurrent::ChannelAll;
using afc::concurrent::VersionPropertyKey;
using afc::concurrent::WorkQueue;
using afc::futures::DeleteNotification;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::OpenDir;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::PathJoin;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IgnoreErrors;
using afc::language::IsError;
using afc::language::LazyValue;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::OptionalFrom;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::EndsWith;
using afc::language::lazy_string::FindFirstOf;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineMetadataKey;
using afc::language::text::LineMetadataMap;
using afc::language::text::LineMetadataValue;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::LineSequenceIterator;
using afc::language::text::MutableLineSequence;
using afc::language::text::SortedLineSequence;
using afc::language::text::SortedLineSequenceUniqueLines;
using afc::language::view::SkipErrors;
using afc::vm::EscapedString;

namespace afc::editor {
namespace {
struct PathPatternMatch {
  using Dir = NonNull<std::unique_ptr<DIR, std::function<void(DIR*)>>>;
  Dir dir;
  // Includes the search_path.
  Path full_path;
  // Excludes the search path.
  LazyString path_pattern;
};

struct DescendDirectoryTreeOutput {
  std::vector<PathPatternMatch> matches;
  // The length of the longest prefix of path that matches a valid directory.
  ColumnNumberDelta valid_prefix_length;
  ColumnNumberDelta valid_proper_prefix_length;
};

// Empty structure to tag the case where no glob is needed.
struct NoSpecialCharacterFound {};

using MatchFunction = std::function<bool(LazyString)>;

// Returns nullptr if pattern doesn't have special characters.
MatchFunction GetComponentMatcher(const LazyString& pattern) {
  if (pattern != LazyString{L"*"}) return nullptr;
  // TODO: Improve this.
  std::wregex regex_filter{L".*"};
  return [regex_filter](LazyString candidate) {
    return static_cast<bool>(
        std::regex_match(candidate.ToString(), regex_filter));
  };
}

std::vector<LazyString> MatchComponent(const PathPatternMatch& state,
                                       LazyString pattern_component) {
  if (MatchFunction filter = GetComponentMatcher(pattern_component);
      filter != nullptr) {
    std::filesystem::path dir_path = state.full_path.ToBytes();
    return std::filesystem::directory_iterator{dir_path} |
           std::views::transform([](auto& entry) {
             return LazyString{
                 FromByteString(entry.path().filename().string())};
           }) |
           std::ranges::to<std::vector>();
  }
  return {pattern_component};
}

DescendDirectoryTreeOutput DescendDirectoryTree(
    Path search_path, LazyString path,
    std::function<ValueOrError<PathPatternMatch::Dir>(Path)> open_dir) {
  VLOG(6) << "Starting search at: " << search_path;
  DescendDirectoryTreeOutput output;
  std::visit(overload{IgnoreErrors{},
                      [&search_path, &output](PathPatternMatch::Dir dir) {
                        output.matches.push_back(
                            PathPatternMatch{.dir = std::move(dir),
                                             .full_path = search_path,
                                             .path_pattern = {}});
                      }},
             open_dir(search_path));
  if (output.matches.empty()) {
    VLOG(5) << "Unable to open search_path: " << search_path;
    return output;
  }

  // We don't use DirectorySplit in order to handle adjacent slashes.
  while (output.valid_prefix_length < path.size()) {
    output.valid_proper_prefix_length = output.valid_prefix_length;
    VLOG(6) << "Iterating at: "
            << path.Substring(ColumnNumber{}, output.valid_prefix_length);
    ColumnNumberDelta component_end =
        FindFirstOf(path, {L'/'}, ColumnNumber{} + output.valid_prefix_length)
            .value_or(ColumnNumber{} + path.size())
            .ToDelta();
    CHECK_GE(component_end, output.valid_prefix_length);
    if (component_end == output.valid_prefix_length) {
      ++output.valid_prefix_length;
      continue;  // Skip consecutive slash.
    }
    LazyString next_component =
        path.Substring(ColumnNumber{} + output.valid_prefix_length,
                       component_end - output.valid_prefix_length);
    std::vector<PathPatternMatch> next_matches =
        output.matches |
        std::views::transform([&](const PathPatternMatch& previous_match) {
          return MatchComponent(previous_match, next_component) |
                 std::views::transform([&open_dir, &previous_match,
                                        &next_matches, &output](
                                           LazyString next_component_match)
                                           -> ValueOrError<PathPatternMatch> {
                   Path next_path = Path::Join(
                       previous_match.full_path,
                       ValueOrDie(PathComponent::New(next_component_match)));
                   LazyString next_pattern =
                       (output.valid_prefix_length.IsZero()
                            ? LazyString{}
                            : previous_match.path_pattern + LazyString{L"/"}) +
                       next_component_match;
                   VLOG(8) << "Considering: " << next_path;
                   DECLARE_OR_RETURN(PathPatternMatch::Dir dir,
                                     open_dir(next_path));
                   return PathPatternMatch{.dir = std::move(dir),
                                           .full_path = next_path,
                                           .path_pattern = next_pattern};
                 });
        }) |
        std::views::join | SkipErrors | std::ranges::to<std::vector>();
    if (next_matches.empty()) return output;
    output.matches = std::move(next_matches);
    output.valid_prefix_length =
        std::min(component_end + ColumnNumberDelta{1}, path.size());
  }
  return output;
}

enum class MatchType { kExact, kPartial };

struct ScanDirectoryInput {
  const FilePredictorOptions& options;
  DIR& dir;
  const std::wregex& noise_regex;
  // The remaining of the pattern after `prefix`, to look up in the directory.
  // May include globs.
  LazyString pattern_suffix;

  // The actual path to `dir`. If the pattern includes matched glob characters,
  // that's not visible here (i.e., they are expanded).
  LazyString path_prefix;

  ColumnNumberDelta pattern_prefix_size;
  DeleteNotification::Value& abort_value;
  PredictorOutput& predictor_output;
  std::function<void(Line, MatchType)> push_output;
};

// Simplified view of lower-level Unix semantics. Good enough for us.
enum class FileType { Directory, Regular, Special };

bool FilterAllows(FileType file_type, const FilePredictorOptions& options) {
  switch (file_type) {
    using enum FileType;
    case Directory:
      return options.directory_filter == FilePredictorOptions::Filter::Include;
    case Regular:
      return true;
    case Special:
      return options.special_file_filter ==
             FilePredictorOptions::Filter::Include;
  }
  LOG(FATAL) << "Invalid file_type value.";
  return true;
}

bool HandlePossibleMatch(const ScanDirectoryInput& input,
                         ColumnNumberDelta match_len, MatchType match_type,
                         std::optional<PathComponent> entry_name,
                         FileType file_type,
                         ColumnNumberDelta& longest_pattern_match) {
  namespace ofp = open_file_position;
  LazyString remaining_suffix =
      input.pattern_suffix.Substring(ColumnNumber{} + match_len);
  std::optional<ofp::Spec> spec = ofp::Parse(
      remaining_suffix, input.options.open_file_position_suffix_mode);
  if (!spec.has_value()) {
    LOG(INFO) << "open_file_position didn't allow match: " << remaining_suffix;
    return false;
  }
  input.predictor_output.found_exact_match |= match_type == MatchType::kExact;
  longest_pattern_match = input.pattern_suffix.size();
  ValueOrError<Path> path_prefix_or_error = Path::New(input.path_prefix);
  if (IsError(path_prefix_or_error) && entry_name == std::nullopt) {
    LOG(INFO) << "No path prefix and no entry name.";
    return false;
  }
  Path full_path = std::visit(
      overload{[&entry_name](Path path_prefix) {
                 return entry_name.has_value()
                            ? Path::Join(path_prefix, entry_name.value())
                            : path_prefix;
               },
               [&entry_name](Error) -> Path { return entry_name.value(); }},
      path_prefix_or_error);
  VLOG(10) << "Interesting entry: " << full_path
           << " exact: " << (match_type == MatchType::kExact)
           << " full: " << full_path << ", spec: " << spec.value();
  if (!FilterAllows(file_type, input.options) ||
      std::regex_match(ToLazyString(full_path).ToString(), input.noise_regex))
    return true;

  LazyString dir_suffix{L"/"};
  LineBuilder line_builder{
      EscapedString::FromString(
          ToLazyString(full_path) +
          (file_type == FileType::Directory && !EndsWith(full_path, dir_suffix)
               ? dir_suffix
               : LazyString{}))
          .EscapedRepresentation()};
  line_builder.SetMetadata(LazyValue<LineMetadataMap>{
      [spec] { return GetLineMetadata(spec.value()); }});
  input.push_output(std::move(line_builder).Build(), match_type);
  return true;
}

// Reads the entire contents of `dir`, looking for files that match `pattern`.
// For any files that do, prepends `prefix` and appends them to `buffer`.
void ScanDirectory(const ScanDirectoryInput input) {
  TRACK_OPERATION(FilePredictor_ScanDirectory);

  VLOG(5) << "Scanning directory \"" << input.path_prefix
          << "\" looking for: " << input.pattern_suffix;
  // The length of the longest prefix of `pattern` that matches an entry.
  ColumnNumberDelta longest_pattern_match;

  HandlePossibleMatch(input, ColumnNumberDelta{}, MatchType::kExact,
                      std::nullopt, FileType::Directory, longest_pattern_match);

  struct dirent* entry;

  const std::wstring pattern_suffix_str = input.pattern_suffix.ToString();
  while ((entry = readdir(&input.dir)) != nullptr) {
    if (input.abort_value.has_value()) return;
    std::string entry_path = entry->d_name;
    auto [pattern_it, entry_it] =
        std::ranges::mismatch(pattern_suffix_str, entry_path);
    ColumnNumberDelta match_len = ColumnNumberDelta{static_cast<int>(
        std::distance(pattern_suffix_str.begin(), pattern_it))};
    if (!HandlePossibleMatch(input, match_len,
                             entry_it == entry_path.end() ? MatchType::kExact
                                                          : MatchType::kPartial,
                             OptionalFrom(PathComponent::New(
                                 LazyString{FromByteString(entry->d_name)})),
                             std::invoke([&entry] {
                               switch (entry->d_type) {
                                 case DT_DIR:
                                   return FileType::Directory;
                                 case DT_REG:
                                   return FileType::Regular;
                                 default:
                                   return FileType::Special;
                               }
                             }),
                             longest_pattern_match)) {
      longest_pattern_match = std::max(longest_pattern_match, match_len);
      VLOG(20) << "The entry " << entry_path
               << " doesn't contain the whole prefix. Longest match: "
               << longest_pattern_match;
    }
  }
  input.predictor_output.longest_prefix =
      std::max(input.predictor_output.longest_prefix,
               input.pattern_prefix_size + longest_pattern_match);
}

futures::Value<PredictorOutput> FilePredictor(FilePredictorOptions options,
                                              PredictorInput predictor_input) {
  LOG(INFO) << "Generating predictions for: " << predictor_input.input;
  return GetSearchPaths(predictor_input.editor)
      .Transform([options, predictor_input](std::vector<Path> search_paths) {
        // We can't use a Path type because this comes from the prompt and ...
        // may not actually be a valid path.
        LazyString path_input = std::visit(
            overload{[&](Error) { return ToLazyString(predictor_input.input); },
                     [&](Path path) {
                       return ToLazyString(
                           predictor_input.editor.expand_path(path));
                     }},
            Path::New(ToLazyString(predictor_input.input)));

        // TODO: Don't use sources_buffers[0], ignoring the other buffers.
        std::wregex noise_regex =
            predictor_input.source_buffers.empty()
                ? std::wregex()
                : std::wregex(predictor_input.source_buffers[0]
                                  .ptr()
                                  ->Read(buffer_variables::directory_noise)
                                  .ToString());
        return predictor_input.editor.thread_pool().Run(std::bind_front(
            [options, path_input, search_paths, noise_regex](
                NonNull<std::shared_ptr<ProgressChannel>> progress_channel,
                DeleteNotification::Value abort_value) mutable {
              if (!path_input.empty() &&
                  path_input.get(ColumnNumber{}) == L'/') {
                search_paths = {Path::Root()};
              } else {
                search_paths =
                    search_paths | std::views::transform([](Path path) {
                      return std::visit(
                          overload{[](infrastructure::AbsolutePath output)
                                       -> ValueOrError<Path> { return output; },
                                   [](Error error) {
                                     return ValueOrError<Path>(error);
                                   }},
                          path.Resolve());
                    }) |
                    SkipErrors | std::ranges::to<std::vector>();

                std::set<Path> already_seen;
                auto [ret, _] = std::ranges::remove_if(
                    search_paths, [&already_seen](const Path& path) {
                      return !already_seen.insert(path).second;
                    });
                search_paths.erase(ret, search_paths.end());
              }

              PredictorOutput predictor_output;
              int matches = 0;
              MutableLineSequence predictions;
              for (const auto& search_path : search_paths) {
                if (abort_value.has_value()) return PredictorOutput{};
                VLOG(4) << "Considering search path: " << search_path;
                DescendDirectoryTreeOutput descend_results =
                    DescendDirectoryTree(search_path, path_input, &OpenDir);
                if (descend_results.matches.empty()) {
                  LOG(WARNING) << "Unable to descend: " << search_path;
                  continue;
                }
                predictor_output.longest_directory_match =
                    std::max(predictor_output.longest_directory_match,
                             ColumnNumberDelta(
                                 descend_results.valid_proper_prefix_length));
                CHECK_LE(descend_results.valid_prefix_length,
                         path_input.size());
                if (descend_results.valid_prefix_length == path_input.size()) {
                  predictor_output.found_exact_match = true;
                } else if (options.match_behavior ==
                           FilePredictorMatchBehavior::kOnlyExactMatch)
                  continue;
                std::ranges::for_each(
                    descend_results.matches,
                    [&](const PathPatternMatch& match) {
                      ScanDirectory(ScanDirectoryInput{
                          .options = options,
                          .dir = match.dir.value(),
                          .noise_regex = noise_regex,
                          .pattern_suffix = path_input.Substring(
                              ColumnNumber{} +
                              descend_results.valid_prefix_length),
                          .path_prefix = match.path_pattern,
                          .pattern_prefix_size =
                              descend_results.valid_prefix_length,
                          .abort_value = abort_value,
                          .predictor_output = predictor_output,
                          .push_output = [&options, &predictions, &matches,
                                          &progress_channel](
                                             Line line, MatchType match_type) {
                            if (options.match_behavior ==
                                    FilePredictorMatchBehavior::
                                        kOnlyExactMatch &&
                                match_type == MatchType::kPartial)
                              return;
                            predictions.push_back(
                                line,
                                MutableLineSequence::ObserverBehavior::kHide);
                            ++matches;
                            if (matches % 100 == 0)
                              progress_channel->Push(ProgressInformation{
                                  .values = {
                                      {VersionPropertyKey{
                                           NON_EMPTY_SINGLE_LINE_CONSTANT(
                                               L"files")},
                                       NonEmptySingleLine(matches).read()}}});
                          }});
                      progress_channel->Push(ProgressInformation{
                          .values = {
                              {VersionPropertyKey{
                                   NON_EMPTY_SINGLE_LINE_CONSTANT(L"files")},
                               NonEmptySingleLine(matches).read()}}});
                    });
              }
              predictions.MaybeEraseEmptyFirstLine();
              SortedLineSequenceUniqueLines output_lines(
                  SortedLineSequence(std::move(predictions).snapshot()));
              predictor_output.contents = output_lines;
              return predictor_output;
            },
            predictor_input.progress_channel,
            std::move(predictor_input.abort_value)));
      });
}
}  // namespace

std::function<futures::Value<PredictorOutput>(PredictorInput input)>
GetFilePredictor(FilePredictorOptions options) {
  return std::bind_front(FilePredictor, options);
}

}  // namespace afc::editor
