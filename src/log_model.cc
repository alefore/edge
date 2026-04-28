#include "src/log_model.h"

using afc::language::Error;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::editor {
LogType::LogType(
    LogTypeName name, NonEmptySingleLine pattern,
    std::unordered_map<LogEntryName, LogEntryConfiguration> entries)
    : name_(std::move(name)),
      regex_(ToLazyString(pattern).ToString()),
      entries_(std::move(entries)),
      capturing_groups_(
          entries_ |
          std::views::transform(
              [](const std::pair<LogEntryName, LogEntryConfiguration>& entry)
                  -> std::pair<LogCapturingGroup, LogEntryName> {
                return std::make_pair(entry.second.capturing_group.value(),
                                      entry.first);
              }) |
          std::ranges::to<std::unordered_map>()) {}

LogTypeName LogType::name() const { return name_; }

std::expected<LogLine, language::Error> LogType::Parse(SingleLine line) {
  std::wstring line_str = ToLazyString(line).ToString();
  std::wsmatch matches;
  if (!std::regex_match(line_str, matches, regex_)) return Error{L"No match."};
  return LogLine{
      .values =
          capturing_groups_ |
          std::views::filter(
              [&matches](
                  const std::pair<LogCapturingGroup, LogEntryName>& group) {
                return group.first.read() < matches.size() &&
                       matches[group.first.read()].matched;
              }) |
          std::views::transform(
              [&matches](const std::pair<LogCapturingGroup, LogEntryName>&
                             group) -> std::pair<LogEntryName, LogEntryValue> {
                return std::make_pair(
                    group.second,
                    LogEntryValue{.value = LazyString{
                                      matches[group.first.read()].str()}});
              }) |
          std::ranges::to<std::unordered_map<LogEntryName, LogEntryValue>>()};
}
}  // namespace afc::editor
