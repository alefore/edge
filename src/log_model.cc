#include "src/log_model.h"

#include "src/language/error/view.h"

using afc::language::Error;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::view::SkipErrors;

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
          std::ranges::to<std::unordered_map>()) {
  LOG(INFO) << "Created LogType with regex: [" << pattern << "]";
}

LogTypeName LogType::name() const { return name_; }

std::expected<LogLine, language::Error> LogType::Parse(SingleLine line) const {
  std::wstring line_str = ToLazyString(line).ToString();
  std::wsmatch matches;
  if (!std::regex_match(line_str, matches, regex_)) return Error{L"No match."};
  return LogLine{
      .values =
          capturing_groups_ |
          std::views::transform(
              [&](const std::pair<LogCapturingGroup, LogEntryName>& group)
                  -> ValueOrError<std::pair<LogEntryName, LogEntryValue>> {
                size_t index = 1 + group.first.read();
                if (index >= matches.size() || !matches[index].matched)
                  return Error{L"No match"};
                return std::make_pair(
                    group.second,
                    LogEntryValue{
                        .value = LazyString{matches[index].str()},
                        .position =
                            ColumnNumber{static_cast<size_t>(std::distance(
                                line_str.cbegin(), matches[index].first))},
                        .size = ColumnNumberDelta{matches[index].length()}});
              }) |
          SkipErrors |
          std::ranges::to<std::unordered_map<LogEntryName, LogEntryValue>>()};
}

std::ostream& operator<<(std::ostream& os, const LogType& value) {
  os << value.name();
  // TODO(P2, 2026-04-28): Output more information.
  return os;
}
}  // namespace afc::editor
