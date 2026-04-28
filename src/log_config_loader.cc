#include "src/log_config_loader.h"

#include <expected>
#include <optional>
#include <ranges>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/language/error/view.h"
#include "src/language/lazy_string/convert.h"
#include "src/language/lazy_string/trim.h"
#include "src/language/text/line.h"

using afc::language::Error;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Trim;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::Range;
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
           return LineNumber{std::get<0>(input)};
         });
}

auto PartitionIntoBlocks(LineSequence lines) {
  // TODO(P2, C++26, 2026-04-28): Avoid the conversion to vector once we can
  // use std::views::concat:
  //    std::views::concat(GetBlockIndices(lines),
  //                       std::views::single(LineNumber{} + lines.size())
  std::vector<LineNumber> indices =
      GetBlockIndices(lines) | std::ranges::to<std::vector>();
  indices.push_back(LineNumber{} + lines.size());
  return indices | std::views::adjacent<2> |
         std::views::transform(
             [lines](std::tuple<LineNumber, LineNumber> entry) {
               return lines.ViewRange(Range{LineColumn{std::get<0>(entry)},
                                            LineColumn{std::get<1>(entry)}});
             });
}

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

  SingleLine pattern = Trim(block.at(LineNumber{1}).contents(), {L' '});
  if (pattern.empty()) return Error{L"Empty pattern found."};

  std::unordered_map<LogEntryName, LogEntryConfiguration> entries;

  for (LineNumber i{2}; i.ToDelta() < block.size(); ++i) {
    SingleLine line_str = Trim(block.at(i).contents(), {L' '});
    if (line_str.empty() || StartsWith(line_str, LazyString{L"#"})) continue;

    std::optional<ColumnNumber> first_space = FindFirstOf(line_str, {L' '});
    if (!first_space.has_value())
      return Error{LazyString{L"Invalid line detected (missing space): "} +
                   line_str};

    DECLARE_OR_RETURN(
        int group_id,
        AsInt(ToLazyString(Trim(
            line_str.Substring(0, first_space.value().ToDelta()), {L' '}))));

    LogEntryName name{
        Trim(line_str.Substring(first_space.value() + ColumnNumberDelta{1}),
             {L' '})};

    entries[name] = LogEntryConfiguration{LogCapturingGroup{group_id}};
  }
  return LogType(log_type_name, pattern, std::move(entries));
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
  if (!errors.empty()) return MergeErrors(errors, L", ");
  return model;
}
}  // namespace afc::editor
// namespace afc::editor