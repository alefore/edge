#pragma once

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
#include "src/language/text/line_sequence.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

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

enum class LogEntryValueType { String, Path };

struct LogEntryConfiguration {
  LogEntryName name;
  std::optional<LogCapturingGroup> capturing_group;
  LogEntryValueType value_type;
};

struct LogEntryValue {
  LogEntryName name;
  std::variant<language::lazy_string::LazyString> value;
  language::lazy_string::ColumnNumber position;
  language::lazy_string::ColumnNumberDelta size;
  LogEntryValueType value_type;

  std::strong_ordering operator<=>(const LogEntryValue&) const = default;
};

struct LogLine {
  // Values must be sorted by `position`.
  std::vector<LogEntryValue> values;

  std::map<LogEntryName, std::vector<LogEntryValue>> ValueGroups() const;
  std::strong_ordering operator<=>(const LogLine&) const = default;
};

std::ostream& operator<<(std::ostream& os, const LogLine& value);

class LogTypeName
    : public language::GhostType<LogTypeName,
                                 language::lazy_string::NonEmptySingleLine> {
 public:
  using GhostType::GhostType;
};

// The VM expressions must evaluate to values of this type.
struct LogViewValueSpec {
  struct ColorsFromHash {
    size_t hash;
  };

  std::variant<ColorsFromHash, infrastructure::screen::Style> style =
      infrastructure::screen::Style{};

  void Merge(const LogViewValueSpec& overlay);
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

class LogLineFormat {
  LogTypeName name_;
  std::wregex regex_;
  // Must be sorted by capturing_group.
  std::vector<LogEntryConfiguration> entries_;
  std::set<LogEntryName> entry_names_;

 public:
  LogLineFormat(LogTypeName name, std::wregex pattern,
                std::vector<LogEntryConfiguration> entries);

  LogTypeName name() const;

  std::set<LogEntryName> entry_names() const;

  std::expected<LogLine, language::Error> Parse(
      language::lazy_string::SingleLine line) const;
};

std::ostream& operator<<(std::ostream& os, const LogLineFormat& value);

enum class LogTypeActivationPolicy { Implicit, Explicit };

class LogType {
  LogTypeName name_;
  std::vector<LogLineFormat> formats_;
  LogTypeActivationPolicy activation_policy_;

 public:
  LogType(LogTypeName name, std::vector<LogLineFormat> formats,
          LogTypeActivationPolicy activation_policy);

  LogTypeName name() const;

  std::set<LogEntryName> entry_names() const;

  LogTypeActivationPolicy activation_policy() const;

  std::expected<LogLine, language::Error> Parse(
      language::lazy_string::SingleLine line) const;
};

std::ostream& operator<<(std::ostream& os, const LogType& value);

struct LogModel {
  std::unordered_map<LogTypeName, LogType> log_types;
  std::unordered_map<LogViewName, LogView> views;

  std::optional<LogTypeName> InferLogType(
      const language::text::LineSequence&) const;
};

// Evaluator that can evaluate expressions scoped to a specific LogLine.
class LogLineEvaluator {
  language::gc::Ptr<vm::Environment> environment_;

 public:
  LogLineEvaluator(language::gc::Ptr<vm::Environment> environment);

  std::expected<language::gc::Root<vm::Value>, language::Error> Evaluate(
      language::gc::Ptr<vm::Expression> expr) const;
};

// Evaluator that can produce LogLineEvaluator instances for a given LogType.
class LogEvaluator {
  language::gc::Pool pool_;
  language::gc::Root<vm::Environment> environment_;
  LogType log_type_;

 public:
  LogEvaluator(LogType log_type);

  std::expected<language::gc::Root<vm::Expression>, language::Error> Compile(
      language::lazy_string::LazyString code) const;

  LogLineEvaluator Enter(const LogLine& log_line);
};

class CompiledLogView {
  LogEvaluator& log_evaluator_;

  // Holds the result of compiling all expressions from log_view.
  using ExpressionMap =
      std::unordered_map<LogEntryName,
                         std::vector<language::gc::Root<vm::Expression>>>;
  ExpressionMap compiled_expressions_;

 public:
  CompiledLogView(LogEvaluator& log_evaluator_, const LogView& log_view);

  std::expected<std::unordered_map<LogEntryName, LogViewValueSpec>,
                language::Error>
  Evaluate(std::unordered_set<LogEntryName> names,
           const LogLine& log_line) const;
};
}  // namespace afc::editor
namespace afc::vm {
template <>
struct VMTypeMapper<editor::LogViewValueSpec> {
  static editor::LogViewValueSpec get(Value& value);
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       editor::LogViewValueSpec value);
  static const types::ObjectName object_type_name;
};
}  // namespace afc::vm
