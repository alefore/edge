#include "src/map_mode.h"

#include <glog/logging.h>

#include <memory>
#include <ranges>
#include <set>

#include "src/command.h"
#include "src/editor.h"
#include "src/help_command.h"
#include "src/language/container.h"
#include "src/language/gc_util.h"
#include "src/language/gc_view.h"
#include "src/language/once_only_function.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"
#include "src/vm/constant_expression.h"
#include "src/vm/function_call.h"
#include "src/vm/types.h"
#include "src/vm/value.h"
#include "src/vm/vm.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::concurrent::WorkQueue;
using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::VectorExtendedChar;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::overload;
using afc::language::VisitPointer;
using afc::language::lazy_string::LazyString;
using afc::vm::Expression;
using afc::vm::Type;
using afc::vm::Value;

namespace afc::editor {
namespace {
template <typename Callback>
class CommandFromFunction : public Command {
  gc::Ptr<Callback> callback_;
  const LazyString description_;

 public:
  CommandFromFunction(gc::Ptr<Callback> callback, LazyString description)
      : callback_(std::move(callback)), description_(std::move(description)) {}

  LazyString Description() const override { return description_; }
  CommandCategory Category() const override {
    return CommandCategory::kCppFunctions();
  }

  void ProcessInput(ExtendedChar) override { callback_.value()(); }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {callback_.object_metadata()};
  }
};

template <typename Callback>
gc::Root<Command> MakeCommandFromFunction(gc::Pool& pool,
                                          gc::Ptr<Callback> callback,
                                          LazyString description) {
  return pool.NewRoot(MakeNonNullUnique<CommandFromFunction<Callback>>(
      std::move(callback), std::move(description)));
}
}  // namespace

/* static */
gc::Root<MapModeCommands> MapModeCommands::New(EditorState& editor_state) {
  gc::Root<MapModeCommands> output = editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<MapModeCommands>(ConstructorAccessTag(), editor_state));
  output.ptr()->Add({L'?'}, NewHelpCommand(editor_state, output.ptr().value(),
                                           L"command mode")
                                .ptr());
  return output;
}

MapModeCommands::MapModeCommands(ConstructorAccessTag,
                                 EditorState& editor_state)
    : editor_state_(editor_state), frames_(1) {}

gc::Root<MapModeCommands> MapModeCommands::NewChild() {
  gc::Root<MapModeCommands> output = MapModeCommands::New(editor_state_);
  output.ptr()->frames_ = frames_;
  output.ptr()->frames_.push_front({});

  // Override the parent's help command, so that bindings added to the child are
  // visible.
  output.ptr()->Add({L'?'}, NewHelpCommand(editor_state_, output.ptr().value(),
                                           L"command mode")
                                .ptr());
  return output;
}

std::map<CommandCategory,
         std::map<std::vector<ExtendedChar>, NonNull<Command*>>>
MapModeCommands::Coallesce() const {
  std::map<CommandCategory,
           std::map<std::vector<ExtendedChar>, NonNull<Command*>>>
      output;
  // Avoid showing unreachable commands.
  std::set<std::vector<ExtendedChar>> already_seen;
  for (const auto& frame : frames_)
    for (const auto& it : frame->commands)
      if (already_seen.insert(it.first).second)
        output[it.second->Category()].insert(
            {it.first, NonNull<Command*>::AddressOf(it.second.value())});
  return output;
}

void MapModeCommands::Add(std::vector<ExtendedChar> name,
                          gc::Ptr<Command> value) {
  CHECK(!frames_.empty());
  frames_.front()->commands.insert({name, std::move(value)});
}

void MapModeCommands::Add(std::vector<ExtendedChar> name,
                          LazyString description, gc::Root<Value> value,
                          gc::Ptr<vm::Environment> environment) {
  const auto& value_type = std::get<vm::types::Function>(value.ptr()->type);
  CHECK(std::holds_alternative<vm::types::Void>(value_type.output.get()));
  CHECK(value_type.inputs.empty()) << "Definition has multiple inputs.";
  Add(name,
      MakeCommandFromFunction(
          editor_state_.gc_pool(),
          gc::BindFront(
              editor_state_.gc_pool(),
              [&editor_state = editor_state_](
                  const gc::Root<vm::Environment> environment_locked,
                  gc::Ptr<vm::Value> value_nested) {
                LOG(INFO) << "Evaluating expression from Value...";
                NonNull<std::shared_ptr<vm::Expression>> expression =
                    NewFunctionCall(
                        NewConstantExpression(value_nested.ToRoot()), {});
                Evaluate(
                    std::move(expression), environment_locked.pool(),
                    environment_locked,
                    [&editor_state](OnceOnlyFunction<void()> callback) {
                      editor_state.work_queue()->Schedule(
                          WorkQueue::Callback{.callback = std::move(callback)});
                    });
              },
              environment.ToWeakPtr(), value.ptr())
              .ptr(),
          description)
          .ptr());
}

void MapModeCommands::Add(std::vector<ExtendedChar> name,
                          std::function<void()> callback,
                          LazyString description) {
  Add(name,
      MakeCommandFromFunction(
          editor_state_.gc_pool(),
          gc::BindFront(editor_state_.gc_pool(), std::move(callback)).ptr(),
          std::move(description))
          .ptr());
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
MapModeCommands::Expand() const {
  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> output;
  for (const NonNull<std::shared_ptr<Frame>>& frame : frames_)
    std::ranges::copy(
        frame->commands | std::views::values | gc::view::ObjectMetadata,
        std::back_inserter(output));
  return output;
}

language::gc::Root<MapMode> MapMode::New(
    language::gc::Pool& pool, language::gc::Ptr<MapModeCommands> commands) {
  return pool.NewRoot(
      MakeNonNullUnique<MapMode>(ConstructorAccessTag(), std::move(commands)));
}

MapMode::MapMode(ConstructorAccessTag, gc::Ptr<MapModeCommands> commands)
    : commands_(std::move(commands)) {}

void MapMode::ProcessInput(ExtendedChar c) {
  current_input_.push_back(c);
  bool reset_input = true;
  for (const auto& frame : commands_->frames_) {
    auto it = frame->commands.lower_bound(current_input_);
    if (it != frame->commands.end() &&
        std::equal(current_input_.begin(), current_input_.end(),
                   it->first.begin())) {
      if (current_input_ == it->first) {
        current_input_.clear();
        it->second->ProcessInput(c);
        return;
      }
      reset_input = false;
    }
  }

  if (reset_input) current_input_.clear();
}

MapMode::CursorMode MapMode::cursor_mode() const {
  return CursorMode::kDefault;
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> MapMode::Expand()
    const {
  return {commands_.object_metadata()};
}

namespace {
const bool map_mode_commands_tests_registration = tests::Register(
    L"MapModeCommands",
    {
        {.name = L"AddNormal",
         .callback =
             [] {
               NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
               gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
               bool executed = false;
               editor->default_commands().ptr()->Add(
                   {L'X'}, LazyString{L"Activates something."},
                   vm::NewCallback(editor->gc_pool(), vm::kPurityTypeUnknown,
                                   [&executed]() { executed = true; }),
                   editor->environment().ptr());
               CHECK(!executed);
               // editor->gc_pool().FullCollect();
               editor->ProcessInput({L'X'});
               CHECK(executed);
             }},
        {.name = L"AddControl",
         .callback =
             [] {
               NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
               gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
               bool executed = false;
               LOG(INFO) << "Adding handler.";
               editor->default_commands().ptr()->Add(
                   {L'A', ControlChar::kPageDown},
                   LazyString{L"Activates something."},
                   vm::NewCallback(editor->gc_pool(), vm::kPurityTypeUnknown,
                                   [&executed]() {
                                     LOG(INFO) << "Executed!";
                                     executed = true;
                                   }),
                   editor->environment().ptr());
               CHECK(!executed);
               LOG(INFO) << "Feeding.";
               editor->ProcessInput({L'A', ControlChar::kPageDown});
               CHECK(executed);
             }},
    });
}  // namespace
}  // namespace afc::editor
