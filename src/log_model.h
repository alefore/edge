#ifndef __AFC_EDITOR_SRC_LOG_MODEL_H__
#define __AFC_EDITOR_SRC_LOG_MODEL_H__

#include <expected>
#include <optional>
#include <regex>
#include <set>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
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
  LogEntryName name;
  std::optional<LogCapturingGroup> capturing_group;
};

struct LogEntryValue {
  LogEntryName name;
  std::variant<language::lazy_string::LazyString> value;
  language::lazy_string::ColumnNumber position;
  language::lazy_string::ColumnNumberDelta size;
};

struct LogLine {
  // Values must be sorted by `position`.
  std::vector<LogEntryValue> values;
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
  std::unordered_map<LogEntryName,
                     std::vector<language::lazy_string::NonEmptySingleLine>>
      expressions;
};

class CompiledLogView {
  language::gc::Root<vm::Environment> environment_;

  // Holds the result of compiling all expressions from log_view.
  using ExpressionMap =
      std::unordered_map<LogEntryName,
                         std::vector<language::gc::Root<vm::Expression>>>;
  ExpressionMap compiled_expressions_;

 public:
  CompiledLogView(language::gc::Root<vm::Environment> environment,
                  const LogView& log_view);

  std::expected<
      std::unordered_map<LogEntryName, infrastructure::screen::LineModifierSet>,
      language::Error>
  Evaluate(std::unordered_set<LogEntryName> names) const;
};

class LogType {
  LogTypeName name_;
  std::wregex regex_;
  // Must be sorted by capturing_group.
  std::vector<LogEntryConfiguration> entries_;
  std::set<LogEntryName> entry_names_;

 public:
  LogType(LogTypeName name, language::lazy_string::NonEmptySingleLine pattern,
          std::vector<LogEntryConfiguration> entries);

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