#ifndef __AFC_EDITOR_SRC_LOG_MODEL_H__
#define __AFC_EDITOR_SRC_LOG_MODEL_H__

#include <expected>
#include <optional>
#include <regex>

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"

namespace afc::editor {
class LogEntryName
    : public language::GhostType<LogEntryName,
                                 language::lazy_string::NonEmptySingleLine> {
 public:
  using GhostType::GhostType;
};

class LogCapturingGroup
    : public language::GhostType<LogCapturingGroup, size_t> {
 public:
  using GhostType::GhostType;
};

struct LogEntryConfiguration {
  std::optional<LogCapturingGroup> capturing_group;
};

struct LogEntryValue {
  std::variant<language::lazy_string::LazyString> value;
};

struct LogLine {
  std::unordered_map<LogEntryName, LogEntryValue> values;
};

class LogTypeName
    : public language::GhostType<LogTypeName,
                                 language::lazy_string::NonEmptySingleLine> {
 public:
  using GhostType::GhostType;
};

class LogType {
  LogTypeName name_;
  std::wregex regex_;
  std::unordered_map<LogEntryName, LogEntryConfiguration> entries_;
  std::unordered_map<LogCapturingGroup, LogEntryName> capturing_groups_;

 public:
  LogType(LogTypeName name, language::lazy_string::NonEmptySingleLine pattern,
          std::unordered_map<LogEntryName, LogEntryConfiguration> entries);

  LogTypeName name() const;

  std::expected<LogLine, language::Error> Parse(
      language::lazy_string::SingleLine line);
};

struct LogModel {
  std::unordered_map<LogTypeName, LogType> log_types;
};
}  // namespace afc::editor

#endif