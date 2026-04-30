#ifndef __AFC_EDITOR_SRC_LOG_MODEL_H__
#define __AFC_EDITOR_SRC_LOG_MODEL_H__

#include <expected>
#include <optional>
#include <regex>
#include <set>

#include "src/language/error/value_or_error.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/vm/types.h"

namespace afc::editor {
class LogEntryName : public language::GhostType<LogEntryName, vm::Identifier> {
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
  language::lazy_string::ColumnNumber position;
  language::lazy_string::ColumnNumberDelta size;
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

class LogViewName
    : public language::GhostType<LogViewName,
                                 language::lazy_string::NonEmptySingleLine> {
 public:
  using GhostType::GhostType;
};

struct LogView {
  LogViewName name;
  // TODO(2026-04-30, P1, log): For now, we hold the uncompiled string. In the
  // future, this should change to the compiled string.
  std::unordered_map<LogEntryName,
                     std::vector<language::lazy_string::NonEmptySingleLine>>
      expressions;
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

  std::set<LogEntryName> entry_names() const;

  std::expected<LogLine, language::Error> Parse(
      language::lazy_string::SingleLine line) const;
};

std::ostream& operator<<(std::ostream& os, const LogType& value);

struct LogModel {
  std::unordered_map<LogTypeName, LogType> log_types;
  std::unordered_map<LogViewName, LogView> views;
};
}  // namespace afc::editor

#endif