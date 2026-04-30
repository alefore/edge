#include "src/parsers/log.h"

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/log_model.h"
#include "src/vm/default_environment.h"
#include "src/vm/environment.h"
#include "src/vm/types.h"
#include "src/vm/value.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifiers;
using afc::infrastructure::screen::LineModifierSet;
using afc::infrastructure::screen::ModifierFromString;
using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::VisitValue;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::LowerCase;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineRange;
using afc::vm::Environment;
using afc::vm::Expression;
using afc::vm::Identifier;
using afc::vm::ToQuotedSingleLine;

namespace afc::editor::parsers {
class LogTreeParser : public TreeParser {
  const LogModel log_model_;
  const LogType log_type_;

 public:
  LogTreeParser(LogModel log_model, LogType log_type)
      : log_model_(std::move(log_model)), log_type_(std::move(log_type)) {}

  ParseTree FindChildren(const language::text::LineSequence& contents,
                         language::text::Range range) {
    // TODO(2026-04-30, P2): Consider moving pool and the default environment to
    // be a class value?
    gc::Pool pool({});
    gc::Root<Environment> environment =
        Environment::New(vm::NewDefaultEnvironment(pool).ptr());
    PrepareEnvironment(environment.ptr());
    ParseTree output = ParseTree(range);
    // TODO(P1, 2026-04-30, trivial): Don't hard-code the view here. Receive it
    // at construction.
    LogViewName view_name{NON_EMPTY_SINGLE_LINE_CONSTANT(L"main")};
    auto view = log_model_.views.find(view_name);
    if (view == log_model_.views.end()) {
      LOG(INFO) << "Unable to find view: " << view_name;
      return output;
    }
    range.ForEachLine([&](LineNumber i) {
      VisitValue(
          log_type_.Parse(contents.at(i).contents().read()), [&](LogLine line) {
            std::ranges::for_each(
                line.values, [&](std::pair<LogEntryName, LogEntryValue> entry) {
                  // TODO(P0, log, 2026-04-30): Remove the call to
                  // Identifier::New from here.
                  // TODO(P1, log, 2026-04-30): Avoid the call to std::get. Use
                  // visit instead?
                  environment->Assign(
                      Identifier::New(entry.first.read()).value(),
                      vm::Value::NewString(
                          environment.pool(),
                          std::get<LazyString>(entry.second.value)));
                });
            std::ranges::for_each(
                line.values, [&](std::pair<LogEntryName, LogEntryValue> entry) {
                  if (auto it = view->second.expressions.find(entry.first);
                      it != view->second.expressions.end()) {
                    ParseTree child(
                        LineRange(LineColumn{i, entry.second.position},
                                  entry.second.size)
                            .read());
                    gc::Root<vm::Environment> sub_environment =
                        Environment::New(environment.ptr());
                    LineModifierSet modifiers;
                    for (NonEmptySingleLine code : it->second) {
                      std::expected<gc::Root<Expression>, Error> expr =
                          vm::CompileString(ToLazyString(code),
                                            sub_environment.ptr());
                      if (!expr) {
                        LOG(INFO) << "Compilation error: " << expr.error();
                        continue;
                      }
                      std::expected<gc::Root<vm::Value>, Error> result =
                          Evaluate(expr->ptr(), sub_environment.ptr(), nullptr)
                              .Get()
                              .value_or(Error{
                                  L"Evaluation future doesn't have a value."});
                      if (result && !result.value()->IsString())
                        result =
                            Error{LazyString{L"Result has wrong type: "} +
                                  ToQuotedSingleLine(result->ptr()->type())};
                      if (!result) {
                        LOG(INFO) << "Error: " << result.error();
                        continue;
                      }
                      VisitValue(
                          NonEmptySingleLine::New(
                              SingleLine::New(result.value()->get_string()))
                              .and_then([](NonEmptySingleLine value) {
                                return ModifierFromString(value);
                              }),
                          [&modifiers](LineModifier modifier) {
                            modifiers.insert(modifier);
                          });
                    }
                    child.set_modifiers(std::move(modifiers));
                    output.PushChild(std::move(child));
                  }
                });
          });
    });
    return output;
  }

 private:
  void PrepareEnvironment(gc::Ptr<Environment>& environment) {
    TRACK_OPERATION(LogTreeParser_PrepareEnvironment);
    // TODO(2026-04-30, P2, easy): Expose the LineModifierSet to vm.
    std::ranges::for_each(
        LineModifiers(),
        [&environment](std::pair<NonEmptySingleLine, LineModifier> data) {
          VisitValue(Identifier::New(LowerCase(data.first)),
                     [&](Identifier id) {
                       VLOG(5) << "Define: " << id << ": " << data.first;
                       environment->Define(
                           id, vm::Value::NewString(environment.pool(),
                                                    ToLazyString(data.first)));
                     });
        });
    environment->Define(IDENTIFIER_CONSTANT(L"default"),
                        vm::Value::NewString(environment.pool(), L""));
    VLOG(2) << "Defining entries for all LogEntryName instances.";
    std::ranges::for_each(log_type_.entry_names(),
                          [&environment](const LogEntryName& name) {
                            std::expected<Identifier, Error> identifier =
                                Identifier::New(name.read());
                            // TODO(2026-04-30, P1, trivial): Consider changing
                            // the LogEntryName to be identifier, so that we
                            // don't convert here.
                            CHECK(identifier);
                            // TODO(2026-04-30, P1): Don't assume string type.
                            environment->DefineUninitialized(
                                identifier.value(), vm::types::String{});
                          });
  }
};

language::NonNull<std::unique_ptr<TreeParser>> NewLogTreeParser(
    LogModel log_model, LogType log_type) {
  LOG(INFO) << "Building LogTreeParser." << log_type;
  return MakeNonNullUnique<LogTreeParser>(std::move(log_model),
                                          std::move(log_type));
}
}  // namespace afc::editor::parsers
