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
LogType::LogType(LogTypeName name, NonEmptySingleLine pattern,
                 std::vector<LogEntryConfiguration> entries)
    : name_(std::move(name)),
      regex_(ToLazyString(pattern).ToString()),
      entries_(std::invoke([&] {
        std::ranges::sort(entries, {}, &LogEntryConfiguration::capturing_group);
        return std::move(entries);
      })),
      entry_names_(entries_ |
                   std::views::transform(&LogEntryConfiguration::name) |
                   std::ranges::to<std::set>()) {
  LOG(INFO) << "Created LogType with regex: [" << pattern << "]";
}

LogTypeName LogType::name() const { return name_; }

std::set<LogEntryName> LogType::entry_names() const {
  return entries_ |
         std::views::transform([](const LogEntryConfiguration& configuration) {
           return configuration.name;
         }) |
         std::ranges::to<std::set>();
}

std::expected<LogLine, language::Error> LogType::Parse(SingleLine line) const {
  TRACK_OPERATION(LogType_Parse);
  std::wstring line_str = ToLazyString(line).ToString();
  std::wsmatch matches;
  if (!std::regex_match(line_str, matches, regex_)) return Error{L"No match."};
  TRACK_OPERATION(LogType_Parse_Process);
  return LogLine{
      .values =
          entries_ |
          std::views::transform([&](const LogEntryConfiguration configuration)
                                    -> ValueOrError<LogEntryValue> {
            size_t index = 1 + configuration.capturing_group->read();
            if (index >= matches.size() || !matches[index].matched)
              return Error{L"No match"};
            return LogEntryValue{
                .name = configuration.name,
                .value = LazyString{matches[index].str()},
                .position = ColumnNumber{static_cast<size_t>(
                    std::distance(line_str.cbegin(), matches[index].first))},
                .size = ColumnNumberDelta{matches[index].length()}};
          }) |
          SkipErrors | std::ranges::to<std::vector>()};
}

std::ostream& operator<<(std::ostream& os, const LogType& value) {
  os << value.name();
  // TODO(P2, 2026-04-28): Output more information.
  return os;
}
}  // namespace afc::editor
