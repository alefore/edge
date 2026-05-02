#include "src/log_model.h"

#include "src/concurrent/protected.h"
#include "src/language/error/view.h"
#include "src/language/text/line.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::concurrent::Protected;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::Error;
using afc::language::NonNull;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::view::SkipErrors;
using afc::vm::Environment;
using afc::vm::Expression;
using container::filter_optional;

using container::CollectExpected;

namespace afc::editor {
CompiledLogView::CompiledLogView(gc::Root<Environment> environment,
                                 const LogView& log_view)
    : environment_(environment),
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
                          TRACK_OPERATION(CompiledLogView_Compile);
                          return vm::CompileString(ToLazyString(code),
                                                   environment.ptr());
                        })));
                return std::pair{data.first, std::move(compile_results)};
              }) |
          SkipErrors | std::ranges::to<std::unordered_map>()) {}

std::expected<std::unordered_map<LogEntryName, LineModifierSet>, Error>
CompiledLogView::Evaluate(std::unordered_set<LogEntryName> names) const {
  return CollectExpected(
             names |
             std::views::transform(
                 [&](LogEntryName name)
                     -> std::expected<std::pair<LogEntryName, LineModifierSet>,
                                      Error> {
                   auto data = compiled_expressions_.find(name);
                   if (data == compiled_expressions_.end())
                     return std::pair{name, LineModifierSet{}};

                   DECLARE_OR_RETURN(
                       std::vector<std::set<LineModifier>> output_vector,
                       CollectExpected(
                           data->second |
                           std::views::transform(
                               [&](gc::Root<Expression> expr)
                                   -> std::expected<std::set<LineModifier>,
                                                    Error> {
                                 TRACK_OPERATION(CompiledLogView_Evaluate_vm);
                                 DECLARE_OR_RETURN(
                                     gc::Root<vm::Value> value,
                                     vm::Evaluate(expr.ptr(),
                                                  environment_.ptr(), nullptr)
                                         .Get()
                                         .value_or(
                                             Error{L"Evaluation future "
                                                   L"doesn't have a value."}));
                                 using Mapper =
                                     vm::VMTypeMapper<NonNull<std::shared_ptr<
                                         Protected<std::set<LineModifier>>>>>;
                                 if (!value->IsObjectType(
                                         Mapper::object_type_name))
                                   return Error{
                                       LazyString{L"Value has wrong type: "} +
                                       vm::ToQuotedSingleLine(value->type())};
                                 TRACK_OPERATION(CompiledLogView_Extract);
                                 return *Mapper::get(value.value())->lock();
                               })));
                   return std::pair{name,
                                    output_vector | std::views::join |
                                        std::ranges::to<LineModifierSet>()};
                 }))
      .transform([](auto values) {
        return values | std::ranges::to<std::unordered_map>();
      });
}

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

std::optional<LogTypeName> LogModel::InferLogType(
    const language::text::LineSequence& lines) const {
  std::vector<LogTypeName> options =
      log_types |
      std::views::transform([&](const std::pair<LogTypeName, LogType>& data)
                                -> std::optional<LogTypeName> {
        if (std::ranges::all_of(lines, [&data](const Line& line) -> bool {
              return data.second.Parse(line.contents()).has_value();
            }))
          return data.first;
        return std::nullopt;
      }) |
      filter_optional | std::ranges::to<std::vector>();
  if (options.size() != 1) return std::nullopt;
  return options[0];
}
}  // namespace afc::editor
