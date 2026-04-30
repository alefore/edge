#include "src/log_config_loader.h"

#include <expected>
#include <optional>
#include <ranges>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/error/view.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/convert.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/lazy_string/trim.h"
#include "src/language/text/line.h"
#include "src/open_files.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::futures::UnwrapVectorFuture;
using afc::infrastructure::Path;
using afc::language::AugmentError;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::PossibleError;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::lazy_string::Trim;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::Range;
using afc::language::view::GetErrors;
using afc::language::view::SkipErrors;

namespace afc::editor {
auto GetBlockIndices(LineSequence lines) {
  return lines | std::views::enumerate |
         std::views::filter([](std::tuple<size_t, const Line&> input) {
           const Line& line = std::get<1>(input);
           return StartsWith(line.contents(), LazyString{L"[type"}) &&
                  EndsWith(line.contents(), LazyString{L"]"});
         }) |
         std::views::transform([](std::tuple<size_t, const Line&> input) {
           return LineColumn{LineNumber{std::get<0>(input)}};
         });
}

std::vector<LineSequence> PartitionIntoBlocks(LineSequence lines) {
  // TODO(P2, C++26, 2026-04-28): Avoid the conversion to vector once we can
  // use std::views::concat:
  //    std::views::concat(GetBlockIndices(lines),
  //                       std::views::single(LineNumber{} + lines.size())
  std::vector<LineColumn> indices =
      GetBlockIndices(lines) | std::ranges::to<std::vector>();
  indices.push_back(lines.range().end());
  return indices | std::views::adjacent<2> |
         std::views::transform(
             [lines](std::tuple<LineColumn, LineColumn> entry) {
               Range range{std::get<0>(entry), std::get<1>(entry)};
               CHECK_LT(range.begin().line, range.end().line);
               return lines.ViewRange(range);
             }) |
         std::ranges::to<std::vector>();
}

namespace {}

std::expected<LogType, Error> ParseLogType(const LineSequence& block) {
  if (block.size() < LineNumberDelta{2}) return Error{L"Short block found."};

  // Each block starts with the [type Name] line
  // TODO(P2, 2026-04-28): Remove comments.
  SingleLine header = Trim(block.at(LineNumber{}).contents(), {L' '});

  // Extract "Name" from "[type Name]"
  // Expected length check and validation should happen here
  static const ColumnNumberDelta kPrefixLength = LazyString{L"[type "}.size();
  DECLARE_OR_RETURN(NonEmptySingleLine log_type_name_str,
                    NonEmptySingleLine::New(
                        Trim(header.Substring(ColumnNumber{} + kPrefixLength,
                                              header.size() - kPrefixLength -
                                                  ColumnNumberDelta{1}),
                             {L' '})));
  LogTypeName log_type_name{log_type_name_str};

  std::optional<NonEmptySingleLine> pattern;
  std::unordered_map<LogEntryName, LogEntryConfiguration> entries;
  static const std::unordered_set<wchar_t> Space = {L' '};
  std::vector<Error> errors =
      block | std::views::drop(1) |
      std::views::transform([&](Line line) -> PossibleError {
        SingleLine line_str = TrimLeft(line.contents(), Space);
        if (line_str.empty() || StartsWith(line_str, LazyString{L"#"}))
          return EmptyValue{};
        std::optional<ColumnNumber> first_colon = FindFirstOf(line_str, {L':'});
        if (!first_colon.has_value())
          return Error{LazyString{L"Invalid line detected (missing colon): "} +
                       line_str};
        SingleLine directive =
            LowerCase(line_str.Substring(first_colon.value()));
        SingleLine value =
            Trim(line_str.Substring(first_colon.value() + ColumnNumberDelta{1}),
                 Space);
        if (value.empty()) return Error{L"Value missing."};
        if (directive == NON_EMPTY_SINGLE_LINE_CONSTANT(L"pattern")) {
          if (pattern.has_value())
            return Error{L"Pattern specified multiple times."};
          DECLARE_OR_RETURN(NonEmptySingleLine pattern_value,
                            AugmentError(L"Producing pattern",
                                         NonEmptySingleLine::New(value)));
          pattern = pattern_value;
          return EmptyValue{};
        }
        if (directive == NON_EMPTY_SINGLE_LINE_CONSTANT(L"group")) {
          std::optional<ColumnNumber> group_id_end = FindFirstOf(value, Space);
          std::optional<SingleLine> entry_name_str =
              group_id_end.transform([&](ColumnNumber pos) {
                return Trim(value.Substring(pos), Space);
              });
          if (!entry_name_str) return Error{L"Expected: entry name."};
          DECLARE_OR_RETURN(NonEmptySingleLine entry_name_non_empty,
                            NonEmptySingleLine::New(entry_name_str.value()));
          return Visit(
              AsInt(ToLazyString(
                  value.Substring(ColumnNumber{}, group_id_end->ToDelta()))),
              [&](int group_id) -> PossibleError {
                entries[LogEntryName{entry_name_non_empty}] =
                    LogEntryConfiguration{LogCapturingGroup{group_id}};
                return EmptyValue{};
              },
              [&errors](Error error) -> PossibleError {
                return AugmentError(L"Invalid group ID", error);
              });
        }
        return Error{LazyString{L"Invalid directive: "} + directive};
      }) |
      GetErrors | std::ranges::to<std::vector>();
  if (!pattern) errors.push_back(Error{L"No pattern specified."});
  if (!errors.empty()) return MergeErrors(errors, L", ");
  return LogType(log_type_name, pattern.value(), std::move(entries));
}

std::expected<LogModel, language::Error> ParseLogConfig(
    const LineSequence& lines) {
  std::vector<Error> errors;
  LogModel model{
      .log_types =
          PartitionIntoBlocks(lines) |
          std::views::transform(
              [&errors](LineSequence block)
                  -> std::expected<std::pair<LogTypeName, LogType>, Error> {
                DECLARE_OR_RETURN(LogType log_type,
                                  CaptureErrors(ParseLogType(block), errors));
                return std::make_pair(log_type.name(), log_type);
              }) |
          SkipErrors | std::ranges::to<std::unordered_map>()};
  LOG(INFO) << "Returning model!";
  if (!errors.empty()) return MergeErrors(errors, L", ");
  return model;
}

futures::ValueOrError<LogModel> LoadModelFromPaths(
    EditorState& editor, language::lazy_string::LazyString paths) {
  LOG(INFO) << "LoadModelFromPaths: " << paths;
  // TODO(P1, 2026-04-29, log): Break paths into comma-separated list of
  // paths.
  DECLARE_OR_RETURN(Path input_path, Path::New(paths));
  return UnwrapVectorFuture(
             editor.edge_path() |
             std::views::transform([&editor, input_path](Path edge_path)
                                       -> futures::ValueOrError<LogModel> {
               DECLARE_OR_RETURN(SingleLine path_pattern,
                                 SingleLine::New(ToLazyString(
                                     Path::Join(edge_path, input_path))));
               return OpenFiles(
                          OpenFilesOptions{
                              .editor = editor,
                              .not_found_handler =
                                  OpenFilesOptions::NotFoundHandler::kIgnore,
                              .path_pattern = path_pattern,
                              .insertion_type =
                                  BuffersList::AddBufferType::kIgnore,
                              .directory_filter =
                                  FilePredictorOptions::Filter::Exclude,
                              .special_file_filter =
                                  FilePredictorOptions::Filter::Exclude})
                   .Transform(
                       [](std::vector<gc::Root<OpenBuffer>> files)
                           -> futures::ValueOrError<gc::Root<OpenBuffer>> {
                         // TODO(P0, 2026-04-29, log): Don't just handle the
                         // first one! Don't ignore the rest.
                         if (files.empty()) return Error{L"No models found."};
                         return files[0]->WaitForEndOfFile();
                       })
                   .Transform([](gc::Root<OpenBuffer> model_root)
                                  -> futures::ValueOrError<LogModel> {
                     LineSequence snapshot = model_root->contents().snapshot();
                     LOG(INFO)
                         << "Parsing log model from " << model_root->name()
                         << ", lines: " << snapshot.size();
                     return model_root->editor().thread_pool().Run(
                         [snapshot] mutable {
                           return ParseLogConfig(std::move(snapshot));
                         });
                   });
             }) |
             std::ranges::to<std::vector>())
      .Transform([](std::vector<ValueOrError<LogModel>> model) {
        // TODO(P0, 2026-04-29, log): Don't just return the first.
        return model[0];
      });
}

}  // namespace afc::editor
// namespace afc::editor