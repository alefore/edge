#include "src/log_model.h"

#include "src/concurrent/protected.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/infrastructure/screen/line_modifier_vm.h"
#include "src/infrastructure/tracker.h"
#include "src/language/error/view.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/text/line.h"
#include "src/vm/default_environment.h"
#include "src/vm/vm.h"

namespace staging = afc::language::staging;
namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::concurrent::Protected;
using afc::infrastructure::screen::Color;
using afc::infrastructure::screen::HashToStyle;
using afc::infrastructure::screen::HashToStyleBold;
using afc::infrastructure::screen::StandardColor;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::compute_hash;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::LowerCase;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::Line;
using afc::language::view::SkipErrors;
using afc::vm::Environment;
using afc::vm::Expression;
using afc::vm::Identifier;

using container::CollectExpected;

namespace afc::editor {
std::map<LogEntryName, std::vector<LogEntryValue>> LogLine::ValueGroups()
    const {
  std::map<LogEntryName, std::vector<LogEntryValue>> output;
  std::ranges::for_each(values, [&](const LogEntryValue& entry) {
    // TODO(P1, log, 2026-04-30): Avoid the call to std::get. Use visit instead?
    output[entry.name].push_back(entry);
  });
  return output;
}

void LogViewValueSpec::Merge(const LogViewValueSpec& overlay) {
  if (ColorsFromHash* this_colors = std::get_if<ColorsFromHash>(&style);
      this_colors) {
    if (const ColorsFromHash* overlay_colors =
            std::get_if<ColorsFromHash>(&overlay.style);
        overlay_colors) {
      this_colors->hash = compute_hash(this_colors->hash, overlay_colors->hash);
    }
  } else {
    if (std::holds_alternative<ColorsFromHash>(overlay.style)) {
      style = overlay.style;
    } else {
      std::get<Style>(style).Merge(std::get<Style>(overlay.style));
    }
  }
}

LogType::LogType(LogTypeName name, std::wregex pattern,
                 std::vector<LogEntryConfiguration> entries,
                 LogTypeActivationPolicy activation_policy)
    : name_(std::move(name)),
      regex_(std::move(pattern)),
      entries_(std::invoke([&] {
        std::ranges::sort(entries, {}, &LogEntryConfiguration::capturing_group);
        return std::move(entries);
      })),
      entry_names_(entries_ |
                   std::views::transform(&LogEntryConfiguration::name) |
                   std::ranges::to<std::set>()),
      activation_policy_(activation_policy) {
  LOG(INFO) << "Created LogType with name: [" << name_ << "]";
}

LogTypeName LogType::name() const { return name_; }

LogTypeActivationPolicy LogType::activation_policy() const {
  return activation_policy_;
}

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
                .size = ColumnNumberDelta{matches[index].length()},
                .value_type = configuration.value_type};
          }) |
          SkipErrors | std::ranges::to<std::vector>()};
}

std::ostream& operator<<(std::ostream& os, const LogType& value) {
  os << value.name();
  // TODO(P2, 2026-04-28): Output more information.
  return os;
}

std::optional<LogTypeName> LogModel::InferLogType(
    const language::text::LineSequence& lines) const {
  LOG(INFO) << "InferLogType with lines: " << lines.size()
            << ", types: " << log_types.size();
  std::vector<LogTypeName> options =
      log_types |
      std::views::filter([&](const std::pair<LogTypeName, LogType>& data) {
        return data.second.activation_policy() ==
                   LogTypeActivationPolicy::Implicit &&
               std::ranges::all_of(
                   lines, [&data](const staging::Value<Line>& line) -> bool {
                     return data.second.Parse(line->contents()).has_value();
                   });
      }) |
      std::views::keys | std::ranges::to<std::vector>();
  if (options.empty()) {
    LOG(INFO) << "No match.";
    return std::nullopt;
  }
  LOG(INFO) << "Matches: " << options.size() << " " << options[0];
  return options[0];
}

LogLineEvaluator::LogLineEvaluator(gc::Ptr<vm::Environment> environment)
    : environment_(environment) {}

std::expected<gc::Root<vm::Value>, Error> LogLineEvaluator::Evaluate(
    gc::Ptr<Expression> expr) const {
  return vm::Evaluate(expr, environment_, nullptr)
      .Get()
      .value_or(Error{L"Evaluation future doesn't have a value."});
}

LogEvaluator::LogEvaluator(LogType log_type)
    : pool_(gc::Pool::Options{}),
      environment_(Environment::New(vm::NewDefaultEnvironment(pool_).ptr())),
      log_type_(std::move(log_type)) {
  TRACK_OPERATION(LogEvaluator_PrepareEnvironment);
  std::ranges::for_each(
      Style::Names(), [&](std::pair<NonEmptySingleLine, Style> data) {
        VisitValue(Identifier::New(LowerCase(data.first)), [&](Identifier id) {
          VLOG(5) << "Define: " << id << ": " << data.first;
          environment_->Define(
              id, vm::VMTypeMapper<LogViewValueSpec>::New(
                      pool_, LogViewValueSpec{.style = Style{data.second}}));
        });
      });
  infrastructure::screen::RegisterLineModifier(pool_, environment_.value());
  environment_->Define(
      IDENTIFIER_CONSTANT(L"log_type"),
      vm::Value::NewString(pool_, ToLazyString(log_type_.name())));
  environment_->Define(IDENTIFIER_CONSTANT(L"default"),
                       vm::VMTypeMapper<LogViewValueSpec>::New(
                           pool_, LogViewValueSpec{.style = Style{}}));

  VLOG(2) << "Defining entries for all LogEntryName instances.";
  std::ranges::for_each(log_type_.entry_names(), [&](const LogEntryName& name) {
    // TODO(2026-04-30, P1): Don't assume string type.
    environment_->DefineUninitialized(name.read(), vm::types::String{});
  });
  environment_->Define(
      IDENTIFIER_CONSTANT(L"hash_block"),
      vm::NewCallback(pool_, vm::kPurityTypePure, [](LazyString input) {
        return LogViewValueSpec{.style =
                                    HashToStyle(std::hash<LazyString>{}(input),
                                                HashToStyleBold::Never)};
      }));
  environment_->Define(
      IDENTIFIER_CONSTANT(L"hash"),
      vm::NewCallback(pool_, vm::kPurityTypePure, [](LazyString input) {
        return LogViewValueSpec{.style = LogViewValueSpec::ColorsFromHash{
                                    std::hash<LazyString>{}(input)}};
      }));
}

std::expected<gc::Root<Expression>, Error> LogEvaluator::Compile(
    LazyString code) const {
  TRACK_OPERATION(LogEvaluator_Compile);
  return vm::CompileString(code, environment_.ptr());
}

LogLineEvaluator LogEvaluator::Enter(const LogLine& log_line) {
  std::unordered_map<LogEntryName, std::vector<LazyString>> values =
      log_line.ValueGroups() |
      std::views::transform(
          [](std::pair<LogEntryName, std::vector<LogEntryValue>> data) {
            return std::make_pair(
                data.first,
                data.second |
                    std::views::transform([](const LogEntryValue& value) {
                      // TODO(P1, log, 2026-04-30): Avoid the call
                      // to std::get. Use visit instead?
                      return std::get<LazyString>(value.value);
                    }) |
                    std::ranges::to<std::vector>());
          }) |
      std::ranges::to<std::unordered_map>();

  TRACK_SCOPE(LogEvaluator_PrepareEnvironment)
  std::ranges::for_each(
      values, [&](std::pair<LogEntryName, std::vector<LazyString>> entry) {
        environment_->Assign(entry.first.read(),
                             vm::Value::NewString(environment_.pool(),
                                                  Concatenate(entry.second)));
      });

  return LogLineEvaluator(environment_.ptr());
}

CompiledLogView::CompiledLogView(LogEvaluator& log_evaluator,
                                 const LogView& log_view)
    : log_evaluator_(log_evaluator),
      compiled_expressions_(
          log_view.expressions |
          std::views::transform(
              [&](std::pair<LogEntryName, std::vector<NonEmptySingleLine>> data)
                  -> std::expected<std::pair<LogEntryName,
                                             std::vector<gc::Root<Expression>>>,
                                   Error> {
                DECLARE_OR_RETURN(
                    std::vector<gc::Root<Expression>> compile_results,
                    CollectExpected(
                        data.second |
                        std::views::transform([&](NonEmptySingleLine code) {
                          return log_evaluator_.Compile(ToLazyString(code));
                        })));
                return std::pair{data.first, std::move(compile_results)};
              }) |
          SkipErrors | std::ranges::to<std::unordered_map>()) {}

std::expected<std::unordered_map<LogEntryName, LogViewValueSpec>, Error>
CompiledLogView::Evaluate(std::unordered_set<LogEntryName> names,
                          const LogLine& log_line) const {
  LogLineEvaluator log_line_evaluator = log_evaluator_.Enter(log_line);
  return CollectExpected(
             names |
             std::views::transform(
                 [&](LogEntryName name)
                     -> std::expected<std::pair<LogEntryName, LogViewValueSpec>,
                                      Error> {
                   auto data = compiled_expressions_.find(name);
                   if (data == compiled_expressions_.end())
                     return std::pair{name, LogViewValueSpec{}};

                   DECLARE_OR_RETURN(
                       std::vector<LogViewValueSpec> output_vector,
                       CollectExpected(
                           data->second |
                           std::views::transform(
                               [&](gc::Root<Expression> expr)
                                   -> std::expected<LogViewValueSpec, Error> {
                                 TRACK_OPERATION(CompiledLogView_Evaluate_vm);
                                 DECLARE_OR_RETURN(
                                     gc::Root<vm::Value> value,
                                     log_line_evaluator.Evaluate(expr.ptr()));
                                 if (!value->IsObjectType(
                                         vm::VMTypeMapper<LogViewValueSpec>::
                                             object_type_name))
                                   return Error{
                                       LazyString{L"Value has wrong type: "} +
                                       vm::ToQuotedSingleLine(value->type())};
                                 TRACK_OPERATION(CompiledLogView_Extract);
                                 return vm::VMTypeMapper<LogViewValueSpec>::get(
                                     value.value());
                               })));
                   return std::pair{name, std::ranges::fold_left(
                                              output_vector, LogViewValueSpec{},
                                              [](LogViewValueSpec aggregator,
                                                 LogViewValueSpec element) {
                                                aggregator.Merge(element);
                                                return aggregator;
                                              })};
                 }))
      .transform([](auto values) {
        return values | std::ranges::to<std::unordered_map>();
      });
}

}  // namespace afc::editor
namespace afc::vm {
/* static */ editor::LogViewValueSpec
VMTypeMapper<editor::LogViewValueSpec>::get(Value& value) {
  return value.get_user_value<editor::LogViewValueSpec>(object_type_name)
      .value();
}

/* static */ language::gc::Root<Value>
VMTypeMapper<editor::LogViewValueSpec>::New(language::gc::Pool& pool,
                                            editor::LogViewValueSpec value) {
  return Value::NewObject(pool, object_type_name,
                          MakeNonNullShared<editor::LogViewValueSpec>(value));
}

const types::ObjectName
    VMTypeMapper<editor::LogViewValueSpec>::object_type_name =
        types::ObjectName{IDENTIFIER_CONSTANT(L"LogViewValueSpec")};
}  // namespace afc::vm
