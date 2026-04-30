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
    std::unordered_map<LogEntryName, std::vector<LogEntryConfiguration>>
        entries)
    : name_(std::move(name)),
      regex_(ToLazyString(pattern).ToString()),
      entries_(std::move(entries)),
      capturing_groups_(
          entries_ |
          std::views::transform(
              [](const std::pair<LogEntryName,
                                 std::vector<LogEntryConfiguration>>& entry) {
                return entry.second |
                       std::views::transform(
                           [&entry](LogEntryConfiguration config) {
                             return std::make_pair(
                                 config.capturing_group.value(), entry.first);
                           }) |
                       std::ranges::to<std::vector>();
              }) |
          std::views::join | std::ranges::to<std::unordered_map>()) {
  LOG(INFO) << "Created LogType with regex: [" << pattern << "]";
}

LogTypeName LogType::name() const { return name_; }

std::set<LogEntryName> LogType::entry_names() const {
  return entries_ |
         std::views::transform([](const auto& data) { return data.first; }) |
         std::ranges::to<std::set>();
}

std::expected<LogLine, language::Error> LogType::Parse(SingleLine line) const {
  std::wstring line_str = ToLazyString(line).ToString();
  std::wsmatch matches;
  if (!std::regex_match(line_str, matches, regex_)) return Error{L"No match."};
  std::unordered_map<LogEntryName, std::vector<LogEntryValue>> values;
  std::ranges::for_each(
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
          SkipErrors,
      [&values](std::pair<LogEntryName, LogEntryValue> data) {
        values[data.first].push_back(std::move(data.second));
      });
  std::ranges::for_each(
      values,
      [](std::pair<const LogEntryName, std::vector<LogEntryValue>>& pair) {
        std::ranges::sort(pair.second, {}, &LogEntryValue::position);
      });
  return LogLine{.values = std::move(values)};
}

std::ostream& operator<<(std::ostream& os, const LogType& value) {
  os << value.name();
  // TODO(P2, 2026-04-28): Output more information.
  return os;
}
}  // namespace afc::editor
