#include "src/file_predictor.h"

extern "C" {
#include <dirent.h>
#include <libgen.h>
#include <sys/types.h>
}

#include <coroutine>
#include <filesystem>
#include <functional>
#include <generator>
#include <regex>
#include <set>
#include <variant>
#include <vector>

#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/buffers_list.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/futures/delete_notification.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/glob.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/error/view.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/lazy_string/trim.h"
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
using afc::futures::UnwrapVectorFuture;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::GlobMatcher;
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
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::EndsWith;
using afc::language::lazy_string::FindFirstOf;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::lazy_string::ToLazyString;
using afc::language::lazy_string::TrimRight;
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
using afc::language::text::NullMutableLineSequenceObserver;
using afc::language::text::SortedLineSequence;
using afc::language::text::SortedLineSequenceUniqueLines;
using afc::language::view::SkipErrors;
using afc::vm::EscapedString;

namespace afc::editor {
std::ostream& operator<<(std::ostream& os,
                         const FilePredictorMatchType& value) {
  switch (value) {
    using enum FilePredictorMatchType;
    case Exact:
      os << "exact match";
      break;
    case Partial:
      os << "partial match";
      break;
  }
  return os;
}

namespace {
struct PathContext {
  // Includes the search_path.
  Path path;

  // Excludes the search path.
  LazyString user_path;

  PathContext Append(const PathComponent& component) const {
    return PathContext{
        .path = Path::Join(path, component),
        .user_path = user_path +
                     (EndsWith(user_path, LazyString{L"/"}) || user_path.empty()
                          ? LazyString{}
                          : LazyString{L"/"}) +
                     ToLazyString(component)};
  }

  bool IsDirectory() const {
    std::error_code ec;
    return std::filesystem::is_directory(ToLazyString(path).ToBytes(), ec);
  }
};

struct DescendDirectoryTreeOutput {
  std::vector<PathContext> matches = {};
  // The length of the longest prefix of path that matches a valid directory.
  ColumnNumberDelta valid_prefix_length = {};
  ColumnNumberDelta valid_proper_prefix_length = {};
};

// Simplified view of lower-level Unix semantics. Good enough for us.
enum class FileType { Directory, Regular, Special };

struct ComponentData {
  const PathComponent path;
  const FileType file_type;
  const GlobMatcher::MatchResults glob_match_results;

  ComponentData(const std::filesystem::directory_entry& entry,
                const GlobMatcher& matcher)
      : path(ValueOrDie(PathComponent::New(
            LazyString{FromByteString(entry.path().filename().string())}))),
        file_type(std::invoke([&entry]() {
          std::error_code ec;
          if (entry.is_directory(ec)) return FileType::Directory;
          ec.clear();
          if (entry.is_regular_file(ec)) return FileType::Regular;
          return FileType::Special;
        })),
        glob_match_results(matcher.Match(ValueOrDie(PathComponent::New(
            LazyString{FromByteString(entry.path().filename().string())})))) {}

  FilePredictorMatchType match_type() const {
    return glob_match_results.component_prefix_size == ToLazyString(path).size()
               ? FilePredictorMatchType::Exact
               : FilePredictorMatchType::Partial;
  }
};

std::generator<ComponentData> ViewComponents(
    Path path, const DeleteNotification::Value& abort_value,
    const GlobMatcher& glob_matcher, FilePredictorMatchType match_type) {
  if (glob_matcher.pattern_type() == GlobMatcher::PatternType::Literal &&
      match_type == FilePredictorMatchType::Exact) {
    ValueOrError<Path> input_path = Path::New(glob_matcher.pattern());
    if (HasValue(input_path)) {
      std::error_code ec;
      std::filesystem::directory_entry entry(
          ToLazyString(Path::Join(path, ValueOrDie(std::move(input_path))))
              .ToBytes(),
          ec);
      if (!ec && entry.exists(ec)) co_yield ComponentData(entry, glob_matcher);
    }
    co_return;
  }

  std::error_code ec;
  auto it =
      std::filesystem::directory_iterator(ToLazyString(path).ToBytes(), ec);
  if (ec) co_return;

  while (it != std::filesystem::end(it)) {
    if (abort_value.has_value()) co_return;
    const auto& entry = *it;
    if (ec) ec.clear();
    ComponentData candidate(entry, glob_matcher);
    if (match_type == FilePredictorMatchType::Partial ||
        candidate.glob_match_results.match_type ==
            GlobMatcher::MatchResults::MatchType::Exact)
      co_yield candidate;
    it.increment(ec);
    if (ec) break;
  }
}

DescendDirectoryTreeOutput DescendDirectoryTree(
    Path search_path, LazyString path,
    const DeleteNotification::Value& abort_value) {
  VLOG(6) << "Starting search at: " << search_path;
  DescendDirectoryTreeOutput output{
      .matches = {PathContext{.path = search_path,
                              .user_path = StartsWith(path, LazyString{L"/"})
                                               ? LazyString{L"/"}
                                               : LazyString{}}}};

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
    const std::optional<ColumnNumber> reminder_start = FindFirstNotOf(
        path, {L'/'},
        ColumnNumber{} +
            std::min(component_end + ColumnNumberDelta{1}, path.size()));
    if (reminder_start == std::nullopt) return output;
    GlobMatcher next_component = GlobMatcher::New(
        path.Substring(ColumnNumber{} + output.valid_prefix_length,
                       component_end - output.valid_prefix_length));
    CHECK(HasValue(PathComponent::New(next_component.pattern())));
    std::vector<PathContext> next_matches =
        output.matches |
        std::views::transform([&](const PathContext& previous_match) {
          return ViewComponents(previous_match.path, abort_value,
                                next_component, FilePredictorMatchType::Exact) |
                 std::views::transform(
                     [&previous_match](const ComponentData& data) {
                       return previous_match.Append(data.path);
                     }) |
                 std::views::filter([](const PathContext& context) {
                   return context.IsDirectory();
                 });
        }) |
        std::views::join | std::ranges::to<std::vector>();
    if (next_matches.empty()) {
      VLOG(5) << "Descend directory stops with matches "
              << output.matches.size() << " at prefix "
              << path.Substring(ColumnNumber{}, output.valid_prefix_length);
      return output;
    }
    output.matches = std::move(next_matches);
    output.valid_prefix_length = reminder_start->ToDelta();
  }
  return output;
}

struct ScanDirectoryInput {
  const FilePredictorOptions& options;
  const std::wregex& noise_regex;
  LazyString input;
  PathContext path_context;
  ColumnNumberDelta pattern_prefix_size;
  const DeleteNotification::Value& abort_value;
  PredictorOutput& predictor_output;
};

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

struct ScanMatchNotAccepted {};
struct ScanMatchIgnored {
  FilePredictorMatchType match_type;
};
struct ScanMatchValid {
  Line line;
  FilePredictorMatchType match_type;
};

using ScanMatch =
    std::variant<ScanMatchNotAccepted, ScanMatchIgnored, ScanMatchValid>;

ScanMatch HandlePossibleMatch(const ScanDirectoryInput& input,
                              const ComponentData& component_data,
                              PathContext path_context,
                              ColumnNumberDelta& longest_pattern_match) {
  namespace ofp = open_file_position;

  LazyString remaining_suffix = std::invoke([&] {
    ColumnNumber next =
        ColumnNumber{} + component_data.glob_match_results.pattern_prefix_size;
    LazyString pattern = input.input;
    if (component_data.file_type == FileType::Directory)  // Skip slashes.
      next = FindFirstNotOf(pattern, {L'/'}, next)
                 .value_or(ColumnNumber{} + pattern.size());
    return pattern.Substring(ColumnNumber{} + next);
  });

  std::optional<ofp::Spec> spec = ofp::Parse(
      remaining_suffix, input.options.open_file_position_suffix_mode);
  if (!spec.has_value()) {
    VLOG(5) << "open_file_position didn't allow match: " << remaining_suffix
            << ", pattern: " << input.input;
    return ScanMatchNotAccepted{};
  }
  longest_pattern_match = input.input.size();
  VLOG(10) << "Interesting entry: " << path_context.path
           << " exact: " << component_data.match_type()
           << ", spec: " << spec.value();
  if (!FilterAllows(component_data.file_type, input.options) ||
      std::regex_match(ToLazyString(path_context.path).ToString(),
                       input.noise_regex))
    return ScanMatchIgnored{.match_type = component_data.match_type()};

  LazyString path_str =
      input.options.output_format == FilePredictorOutputFormat::Input
          ? path_context.user_path
          : ToLazyString(path_context.path);
  static LazyString dir_suffix{L"/"};
  if (component_data.file_type == FileType::Directory &&
      !EndsWith(path_str, dir_suffix))
    path_str += dir_suffix;
  return ScanMatchValid{
      .line =
          LineBuilder{
              EscapedString::FromString(path_str).EscapedRepresentation()}
              .SetMetadata(LazyValue<LineMetadataMap>{
                  [spec] { return GetLineMetadata(spec.value()); }})
              .Build(),
      .match_type = component_data.match_type()};
}

// Reads the entire contents of `dir`, looking for files that match `pattern`.
// For any files that do, prepends `prefix` and appends them to `buffer`.
std::generator<ScanMatch> ScanDirectory(const ScanDirectoryInput input) {
  TRACK_OPERATION(FilePredictor_ScanDirectory);

  VLOG(5) << "Scanning directory \"" << input.path_context.path
          << "\" looking for: " << input.input;
  // The length of the longest prefix of `pattern` that matches an entry.
  ColumnNumberDelta longest_pattern_match;

  GlobMatcher glob_matcher = GlobMatcher::New(input.input);

  for (const ComponentData& component :
       ViewComponents(input.path_context.path, input.abort_value, glob_matcher,
                      input.options.match_type)) {
    VLOG(8) << "Dir match: " << component.glob_match_results;
    ScanMatch file_result = HandlePossibleMatch(
        input, component, input.path_context.Append(component.path),
        longest_pattern_match);
    if (std::holds_alternative<ScanMatchNotAccepted>(file_result))
      longest_pattern_match =
          std::max(longest_pattern_match,
                   component.glob_match_results.pattern_prefix_size);
    co_yield std::move(file_result);
  }
  input.predictor_output.longest_prefix =
      std::max(input.predictor_output.longest_prefix,
               input.pattern_prefix_size + longest_pattern_match);
}

futures::Value<LineSequence> GetSearchPathsBuffer(EditorState& editor_state,
                                                  const Path& edge_path) {
  BufferName buffer_name{LazyString{L"- search paths"}};
  return VisitOptional(
             [](gc::Root<OpenBuffer> buffer) { return futures::Past(buffer); },
             [&] {
               return OpenOrCreateFile(
                          OpenFileOptions{
                              .editor_state = editor_state,
                              .name = buffer_name,
                              .path = Path::Join(
                                  edge_path, ValueOrDie(Path::New(
                                                 LazyString{L"search_paths"}))),
                              .insertion_type =
                                  BuffersList::AddBufferType::kIgnore})
                   .Transform([&editor_state](gc::Root<OpenBuffer> buffer) {
                     buffer->Set(buffer_variables::save_on_close, true);
                     buffer->Set(
                         buffer_variables::trigger_reload_on_buffer_write,
                         false);
                     buffer->Set(buffer_variables::show_in_buffers_list, false);
                     if (!editor_state.has_current_buffer()) {
                       editor_state.set_current_buffer(
                           buffer, CommandArgumentModeApplyMode::kFinal);
                     }
                     return buffer;
                   });
             },
             editor_state.buffer_registry().Find(buffer_name))
      .Transform([](gc::Root<OpenBuffer> buffer) {
        return buffer->WaitForEndOfFile();
      })
      .Transform([](gc::Root<OpenBuffer> buffer) {
        return buffer->contents().snapshot();
      });
}

futures::Value<std::vector<Path>> GetSearchPaths(EditorState& editor_state) {
  return UnwrapVectorFuture(
             editor_state.edge_path() |
             std::views::transform([&editor_state](Path edge_path) {
               return GetSearchPathsBuffer(editor_state, edge_path)
                   .Transform([&editor_state](LineSequence buffer_contents)
                                  -> std::vector<Path> {
                     return buffer_contents |
                            std::views::transform([](const Line& line) {
                              return Path::New(line.contents().read());
                            }) |
                            language::view::SkipErrors |
                            std::views::transform([&editor_state](
                                                      const Path& path) {
                              return editor_state.expand_path(path).Resolve();
                            }) |
                            SkipErrors | std::views::transform([](Path path) {
                              return path.Resolve();
                            }) |
                            language::view::SkipErrors |
                            std::ranges::to<std::vector<Path>>();
                   });
             }) |
             std::ranges::to<std::vector>())
      .Transform([](std::vector<std::vector<Path>> paths) {
        Path local_resolved_path =
            std::optional<Path>(OptionalFrom(Path::LocalDirectory().Resolve()))
                .value_or(Path::LocalDirectory());
        std::vector<Path> output = {local_resolved_path};
        std::unordered_set<Path> seen =
            output | std::ranges::to<std::unordered_set>();
        std::ranges::for_each(paths | std::views::join, [&](Path path) {
          if (seen.insert(path).second) output.push_back(std::move(path));
        });
        return output;
      });
}

void PredictInSearchPath(const FilePredictorOptions& options, Path search_path,
                         LazyString path_input, std::wregex noise_regex,
                         const DeleteNotification::Value& abort_value,
                         MutableLineSequence& predictions,
                         PredictorOutput& predictor_output) {
  VLOG(4) << "Considering search path: " << search_path;
  DescendDirectoryTreeOutput descend_results =
      DescendDirectoryTree(search_path, path_input, abort_value);
  if (descend_results.matches.empty()) {
    LOG(WARNING) << "Unable to descend: " << search_path;
    return;
  }
  predictor_output.longest_directory_match =
      std::max(predictor_output.longest_directory_match,
               ColumnNumberDelta(descend_results.valid_proper_prefix_length));
  CHECK_LE(descend_results.valid_prefix_length, path_input.size());
  if (descend_results.valid_prefix_length == path_input.size())
    predictor_output.found_exact_match = true;
  std::ranges::for_each(
      descend_results.matches |
          std::views::transform([&](const PathContext& match) {
            return ScanDirectory(ScanDirectoryInput{
                .options = options,
                .noise_regex = noise_regex,
.input = path_input.Substring(
                    ColumnNumber{} + descend_results.valid_prefix_length),
                .path_context = match,
                .pattern_prefix_size = descend_results.valid_prefix_length,
                .abort_value = abort_value,
                .predictor_output = predictor_output});
          }) |
          std::views::join | container::filter_variant<ScanMatchValid> |
          std::views::filter(options.match_type == FilePredictorMatchType::Exact
                                 ? [](const ScanMatchValid& v) {
                                     return v.match_type ==
                                            FilePredictorMatchType::Exact;
                                   }
                                 : [](const auto&) { return true; }) |
          std::views::take(
              options.match_limit.has_value()
                  ? static_cast<int64_t>(options.match_limit.value())
                  : std::numeric_limits<int64_t>::max()),
      [&](ScanMatchValid scan_result) {
        predictions.push_back(std::move(scan_result.line));
        predictor_output.found_exact_match |=
            scan_result.match_type == FilePredictorMatchType::Exact;
      });
}

const bool predict_in_search_path_tests_registration =
    tests::Register(L"PredictInSearchPath", [] {
      auto make_hierarchy = [] {
        namespace fs = std::filesystem;
        std::string path_template =
            (fs::temp_directory_path() / "edge_test_XXXXXX").string();
        CHECK(mkdtemp(path_template.data()) != nullptr);
        fs::path root{path_template};
        std::vector<std::string> files = {
            "animals/dog.txt",    "animals/dingo.txt", "animals/cat.txt",
            "animals/rabbit.txt", "plants/orchid.txt", "plants/rose.txt",
        };
        for (const auto& rel_path : files) {
          fs::path full_path = root / rel_path;
          fs::create_directories(full_path.parent_path());
          std::ofstream{full_path} << "";  // Create file.
        }
        return ValueOrDie(Path::New(LazyString{FromByteString(root.string())}));
      };
      struct Expectations {
        std::optional<std::vector<std::wstring>> predictions = std::nullopt;
        std::optional<ColumnNumberDelta> longest_prefix = std::nullopt;
        std::optional<bool> found_exact_match = std::nullopt;
        static Expectations Predictions(std::vector<std::wstring> value) {
          return Expectations{.predictions = value};
        }
        static Expectations NoPrediction() { return Predictions({L""}); }
        static Expectations ExactMatch(bool value) {
          return {.found_exact_match = value};
        }
      };
      auto test = [&](std::wstring name, std::wstring prediction,
                      Expectations expectations,
                      FilePredictorOptions file_predictor_options =
                          FilePredictorOptions{}) -> std::vector<tests::Test> {
        auto internal_test = [=](FilePredictorOutputFormat output_format) {
          std::wstring actual_name = name;
          if (output_format == FilePredictorOutputFormat::SearchPathAndInput)
            actual_name += std::wstring(L"WithSearchPath");
          FilePredictorOptions actual_options = file_predictor_options;
          actual_options.output_format = output_format;
          return tests::Test{
              .name = actual_name, .callback = [=] {
                Path root = make_hierarchy();
                MutableLineSequence predictions;
                DeleteNotification delete_notification;
                PredictorOutput predictor_output;
                PredictInSearchPath(actual_options, root,
                                    LazyString(prediction), std::wregex(),
                                    delete_notification.listenable_value(),
                                    predictions, predictor_output);
                predictions.MaybeEraseEmptyFirstLine();
                if (expectations.predictions.has_value()) {
                  CHECK_EQ(
                      SortedLineSequenceUniqueLines(
                          SortedLineSequence(std::move(predictions).snapshot()))
                          .read()
                          .lines()
                          .ToLazyString(),
                      LineSequence::ForTests(
                          std::invoke([&] -> std::vector<std::wstring> {
                            switch (output_format) {
                              using enum FilePredictorOutputFormat;
                              case SearchPathAndInput:
                                if (expectations.predictions.value() ==
                                    std::vector<std::wstring>{L""})
                                  return expectations.predictions.value();
                                return expectations.predictions.value() |
                                       std::views::transform(
                                           [root](std::wstring path) {
                                             return (ToLazyString(root) +
                                                     LazyString{L"/"} +
                                                     LazyString(path))
                                                 .ToString();
                                           }) |
                                       std::ranges::to<std::vector>();

                              case Input:
                                return expectations.predictions.value();
                            }
                            LOG(FATAL);
                            return {};
                          }))
                          .ToLazyString());
                }
                if (expectations.longest_prefix.has_value())
                  CHECK_EQ(predictor_output.longest_prefix,
                           expectations.longest_prefix.value());
                if (expectations.found_exact_match.has_value())
                  CHECK_EQ(predictor_output.found_exact_match,
                           expectations.found_exact_match.value());
              }};
        };
        return {
            internal_test(FilePredictorOutputFormat::SearchPathAndInput),
            internal_test(FilePredictorOutputFormat::Input),
        };
      };
      return std::vector({
                 test(L"NoMatch", L"foo", Expectations::NoPrediction()),
                 test(L"NoMatchInDir", L"animals/apple.txt",
                      Expectations::NoPrediction()),
                 test(L"ExactMatchWildcard", L"animals/rab*.txt",
                      Expectations::Predictions({L"animals/rabbit.txt"})),
                 test(L"ExactMatch", L"animals/dog.txt",
                      Expectations::Predictions({L"animals/dog.txt"})),
                 test(L"MultipleMatch", L"animals/*.txt",
                      Expectations::Predictions(
                          {L"animals/cat.txt", L"animals/dingo.txt",
                           L"animals/dog.txt", L"animals/rabbit.txt"})),
                 test(L"SingleDirMatch", L"a*/dog.txt",
                      Expectations::Predictions({L"animals/dog.txt"})),
                 test(L"MultiDirMatch", L"*a*/*o*.txt",
                      Expectations::Predictions(
                          {L"animals/dingo.txt", L"animals/dog.txt",
                           L"plants/orchid.txt", L"plants/rose.txt"})),
                 test(L"PrefixMatch", L"anim*/d",
                      Expectations::Predictions(
                          {L"animals/dingo.txt", L"animals/dog.txt"})),
                 test(L"PrefixMatchExactly", L"anim*/d",
                      Expectations::NoPrediction(),
                      FilePredictorOptions{.match_type =
                                               FilePredictorMatchType::Exact}),
                 test(L"DirPrefix", L"anim",
                      Expectations::Predictions({L"animals/"})),
                 test(L"DirPrefixExact", L"anim", Expectations::NoPrediction(),
                      FilePredictorOptions{.match_type =
                                               FilePredictorMatchType::Exact}),
                 test(L"DirExact", L"plants",
                      Expectations::Predictions({L"plants/"})),
                 test(L"DirExactSlash", L"plants/",
                      Expectations::Predictions({L"plants/"})),
                 test(L"DirExactMatchExact", L"plants",
                      Expectations::Predictions({L"plants/"}),
                      FilePredictorOptions{.match_type =
                                               FilePredictorMatchType::Exact}),
                 test(L"DirExactSlashMatchExact", L"plants/",
                      Expectations::Predictions({L"plants/"}),
                      FilePredictorOptions{.match_type =
                                               FilePredictorMatchType::Exact}),
                 test(L"TrailingComponentsAfterFile",
                      L"animals/dog.txt/foo/bar", Expectations::Predictions({}),
                      FilePredictorOptions{.match_type =
                                               FilePredictorMatchType::Exact}),
                 test(L"WithPosition", L"*/dog.txt:5",
                      Expectations::Predictions({L"animals/dog.txt"})),
                 test(L"WithGarbageAllow", L"*/dog.txt:5: nothing",
                      Expectations::Predictions({L"animals/dog.txt"}),
                      FilePredictorOptions{
                          .open_file_position_suffix_mode =
                              open_file_position::SuffixMode::Allow}),
                 test(L"WithGarbageDisallow", L"*/dog.txt:5: nothing",
                      Expectations::NoPrediction()),
                 test(L"PrefixPartial", L"*/dXX",
                      {.longest_prefix = ColumnNumberDelta{3}}),
                 test(L"PrefixFull", L"*/dog.*",
                      {.longest_prefix = ColumnNumberDelta{7}}),
                 test(L"PrefixDir", L"plants",
                      {.longest_prefix = ColumnNumberDelta{6}}),
                 test(L"PrefixDirWithSlash", L"plants/",
                      {.longest_prefix = ColumnNumberDelta{7}}),
                 test(L"FindExactMatchLiteral", L"plants/rose.txt",
                      Expectations::ExactMatch(true),
                      FilePredictorOptions{
                          .open_file_position_suffix_mode =
                              open_file_position::SuffixMode::Allow}),
                 test(L"FindExactMatchWildcard", L"plants/r*.txt",
                      Expectations::ExactMatch(true)),
                 test(L"FindExactMatchDirectory", L"plants",
                      Expectations::ExactMatch(true)),
                 test(L"FindExactMatchDirectorySlash", L"plants/",
                      Expectations::ExactMatch(true)),
                 test(L"FindPartialMatch", L"plants/r",
                      Expectations::ExactMatch(false)),
                 test(L"FindExactMatchLiteralWithPosition",
                      L"plants/rose.txt:12", Expectations::ExactMatch(true)),
             }) |
             std::views::join | std::ranges::to<std::vector>();
    }());

class ProgressChannelNotifier : public NullMutableLineSequenceObserver {
  const NonNull<std::shared_ptr<ProgressChannel>> progress_channel_;
  size_t matches_ = 0;

 public:
  ProgressChannelNotifier(
      NonNull<std::shared_ptr<ProgressChannel>> progress_channel)
      : progress_channel_(progress_channel) {}

  ~ProgressChannelNotifier() override { Notify(); }
  void LinesInserted(LineNumber, LineNumberDelta size) override {
    CHECK_EQ(size, LineNumberDelta{1});
    ++matches_;
    if (matches_ % 100ul == 0ul) Notify();
  }

 private:
  void Notify() {
    progress_channel_->Push(ProgressInformation{
        .values = {
            {VersionPropertyKey{NON_EMPTY_SINGLE_LINE_CONSTANT(L"files")},
             NonEmptySingleLine(matches_).read()}}});
  }
};

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
              predictor_output.contents = SortedLineSequenceUniqueLines(
                  SortedLineSequence(std::invoke([&] {
                    MutableLineSequence predictions(
                        MakeNonNullShared<ProgressChannelNotifier>(
                            progress_channel));
                    std::ranges::for_each(search_paths, [&](Path search_path) {
                      PredictInSearchPath(options, search_path, path_input,
                                          noise_regex, abort_value, predictions,
                                          predictor_output);
                    });
                    predictions.MaybeEraseEmptyFirstLine();
                    return abort_value.has_value()
                               ? LineSequence{}
                               : std::move(predictions).snapshot();
                  })));
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
