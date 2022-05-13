#include "src/map_mode.h"

#include <glog/logging.h>

#include <memory>
#include <set>

#include "src/command.h"
#include "src/editor.h"
#include "src/help_command.h"
#include "src/language/safe_types.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;
using vm::Expression;
using vm::Value;
using vm::VMType;

namespace gc = language::gc;

namespace {
template <typename Callback>
class CommandFromFunction : public Command {
 public:
  CommandFromFunction(Callback callback, wstring description)
      : callback_(std::move(callback)), description_(std::move(description)) {}

  std::wstring Description() const override { return description_; }
  std::wstring Category() const override {
    return L"C++ Functions (Extensions)";
  }

  void ProcessInput(wint_t) override { callback_(); }

 private:
  Callback callback_;
  const wstring description_;
};

template <typename Callback>
NonNull<std::unique_ptr<Command>> MakeCommandFromFunction(Callback callback,
                                                          wstring description) {
  return MakeNonNullUnique<CommandFromFunction<Callback>>(
      std::move(callback), std::move(description));
}
}  // namespace

MapModeCommands::MapModeCommands(EditorState& editor_state)
    : editor_state_(editor_state), frames_(1) {
  Add(L"?", NewHelpCommand(editor_state_, *this, L"command mode"));
}

NonNull<std::unique_ptr<MapModeCommands>> MapModeCommands::NewChild() {
  auto output = MakeNonNullUnique<MapModeCommands>(editor_state_);
  output->frames_ = frames_;
  output->frames_.push_front({});

  // Override the parent's help command, so that bindings added to the child are
  // visible.
  output->Add(L"?", NewHelpCommand(editor_state_, *output, L"command mode"));
  return output;
}

std::map<wstring, std::map<wstring, NonNull<Command*>>>
MapModeCommands::Coallesce() const {
  std::map<wstring, std::map<wstring, NonNull<Command*>>> output;
  std::set<wstring> already_seen;  // Avoid showing unreachable commands.
  for (const auto& frame : frames_) {
    for (const auto& it : frame->commands) {
      if (already_seen.insert(it.first).second) {
        output[it.second->Category()].insert({it.first, it.second.get()});
      }
    }
  }
  return output;
}

void MapModeCommands::Add(wstring name,
                          NonNull<std::unique_ptr<Command>> value) {
  CHECK(!frames_.empty());
  frames_.front()->commands.insert({name, std::move(value)});
}

void MapModeCommands::Add(wstring name, wstring description,
                          gc::Root<Value> value,
                          gc::Root<vm::Environment> environment) {
  CHECK(value.ptr()->type.type == VMType::Type::kFunction);
  CHECK(value.ptr()->type.type_arguments ==
        std::vector<VMType>({VMType::Void()}));

  Add(name,
      MakeCommandFromFunction(
          std::bind_front(
              [&editor_state = editor_state_, environment](
                  NonNull<std::unique_ptr<vm::Expression>>& expression) {
                LOG(INFO) << "Evaluating expression from Value...";
                Evaluate(*expression, environment.pool(), environment,
                         [&editor_state](std::function<void()> callback) {
                           editor_state.work_queue()->Schedule(callback);
                         });
              },
              NewFunctionCall(NewConstantExpression(std::move(value)), {})),
          description));
}

void MapModeCommands::Add(wstring name, std::function<void()> callback,
                          wstring description) {
  Add(name,
      MakeCommandFromFunction(std::move(callback), std::move(description)));
}

MapMode::MapMode(NonNull<std::shared_ptr<MapModeCommands>> commands)
    : commands_(std::move(commands)) {}

void MapMode::ProcessInput(wint_t c) {
  current_input_.push_back(c);

  bool reset_input = true;
  for (const auto& frame : commands_->frames_) {
    auto it = frame->commands.lower_bound(current_input_);
    if (it != frame->commands.end() &&
        std::equal(current_input_.begin(), current_input_.end(),
                   it->first.begin())) {
      if (current_input_ == it->first) {
        current_input_ = L"";
        it->second->ProcessInput(c);
        return;
      }
      reset_input = false;
    }
  }

  if (reset_input) {
    current_input_ = L"";
  }
}

MapMode::CursorMode MapMode::cursor_mode() const {
  return CursorMode::kDefault;
}

}  // namespace afc::editor
