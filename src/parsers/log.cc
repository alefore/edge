#include "src/parsers/log.h"

#include "src/concurrent/protected.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/infrastructure/screen/line_modifier_vm.h"
#include "src/language/error/value_or_error.h"
#include "src/language/error/view.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/log_model.h"
#include "src/vm/default_environment.h"
#include "src/vm/environment.h"
#include "src/vm/types.h"
#include "src/vm/value.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;
using afc::concurrent::Protected;
using afc::infrastructure::screen::HashToModifiers;
using afc::infrastructure::screen::HashToModifiersBold;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifiers;
using afc::infrastructure::screen::LineModifierSet;
using afc::infrastructure::screen::ModifierFromString;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::ValueOrError;
using afc::language::VisitValue;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::LowerCase;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineRange;
using afc::vm::Environment;
using afc::vm::Expression;
using afc::vm::Identifier;
using afc::vm::ToQuotedSingleLine;

namespace afc::editor::parsers {
class LogTreeParser : public TreeParser {
  const LogType log_type_;
  const LogView log_view_;

 public:
  LogTreeParser(LogType log_type, LogView log_view)
      : log_type_(std::move(log_type)), log_view_(std::move(log_view)) {}

  StateBoundary state_boundary() const override { return StateBoundary::Line; }

  ParseTree FindChildren(const language::text::LineSequence& contents,
                         language::text::Range range) {
    TRACK_OPERATION(LogTreeParser_FindChildren);
    // TODO(2026-04-30, P2): Consider moving pool and the default environment to
    // be a class value?
    gc::Pool pool({});
    gc::Root<Environment> environment =
        Environment::New(vm::NewDefaultEnvironment(pool).ptr());
    PrepareEnvironment(environment.ptr());
    ParseTree output = ParseTree(range);
    CompiledLogView compiled_log_view{environment, log_view_};
    range.ForEachLine([&](LineNumber i) {
      VisitValue(
          log_type_.Parse(contents.at(i).contents().read()), [&](LogLine line) {
            std::unordered_map<LogEntryName, std::vector<LazyString>> values;
            {
              TRACK_OPERATION(LogTreeParser_FindChildren_AggregateValues);
              std::ranges::for_each(line.values,
                                    [&](const LogEntryValue& entry) {
                                      // TODO(P1, log, 2026-04-30): Avoid the
                                      // call to std::get. Use visit instead?
                                      values[entry.name].push_back(
                                          std::get<LazyString>(entry.value));
                                    });
            }

            {
              TRACK_OPERATION(LogTreeParser_FindChildren_PrepareEnvironment);
              std::ranges::for_each(
                  values,
                  [&](std::pair<LogEntryName, std::vector<LazyString>> entry) {
                    environment->Assign(
                        entry.first.read(),
                        vm::Value::NewString(environment.pool(),
                                             Concatenate(entry.second)));
                  });
            }

            std::expected<std::unordered_map<LogEntryName, LineModifierSet>,
                          Error>
                modifiers_map = compiled_log_view.Evaluate(
                    values | std::views::keys |
                    std::ranges::to<std::unordered_set>());
            if (!modifiers_map) {
              LOG(INFO) << "Error: " << modifiers_map.error();
              return;
            }

            TRACK_OPERATION(LogTreeParser_ProduceTrees);
            std::ranges::for_each(line.values, [&](const LogEntryValue& data) {
              ParseTree child(
                  LineRange(LineColumn{i, data.position}, data.size).read());
              child.set_modifiers(modifiers_map->find(data.name)->second);
              output.PushChild(std::move(child));
            });
          });
    });
    return output;
  }

 private:
  void PrepareEnvironment(gc::Ptr<Environment>& environment) {
    TRACK_OPERATION(LogTreeParser_PrepareEnvironment);
    // TODO(2026-04-30, P2, easy): Expose the LineModifierSet to vm.
    using Mapper = vm::VMTypeMapper<
        NonNull<std::shared_ptr<Protected<std::set<LineModifier>>>>>;
    std::ranges::for_each(
        LineModifiers(),
        [&environment](std::pair<NonEmptySingleLine, LineModifier> data) {
          VisitValue(
              Identifier::New(LowerCase(data.first)), [&](Identifier id) {
                VLOG(5) << "Define: " << id << ": " << data.first;
                environment->Define(
                    id,
                    Mapper::New(
                        environment.pool(),
                        MakeNonNullShared<Protected<std::set<LineModifier>>>(
                            std::set<LineModifier>{data.second})));
              });
        });
    infrastructure::screen::RegisterLineModifier(environment.pool(),
                                                 environment.value());
    environment->Define(IDENTIFIER_CONSTANT(L"log_type"),
                        vm::Value::NewString(environment.pool(),
                                             ToLazyString(log_type_.name())));
    environment->Define(
        IDENTIFIER_CONSTANT(L"default"),
        Mapper::New(environment.pool(),
                    MakeNonNullShared<Protected<std::set<LineModifier>>>()));
    VLOG(2) << "Defining entries for all LogEntryName instances.";
    std::ranges::for_each(
        log_type_.entry_names(), [&environment](const LogEntryName& name) {
          // TODO(2026-04-30, P1): Don't assume string type.
          environment->DefineUninitialized(name.read(), vm::types::String{});
        });
    environment->Define(
        IDENTIFIER_CONSTANT(L"hash"),
        vm::NewCallback(
            environment.pool(), vm::kPurityTypePure, [](LazyString input) {
              return MakeNonNullShared<Protected<std::set<LineModifier>>>(
                  HashToModifiers(std::hash<LazyString>{}(input),
                                  HashToModifiersBold::Never) |
                  std::ranges::to<std::set>());
            }));
  }
};

language::NonNull<std::unique_ptr<TreeParser>> NewLogTreeParser(
    LogType log_type, LogView log_view) {
  LOG(INFO) << "Building LogTreeParser." << log_type;
  return MakeNonNullUnique<LogTreeParser>(std::move(log_type),
                                          std::move(log_view));
}
}  // namespace afc::editor::parsers
