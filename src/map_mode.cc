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

class EditorState;
MapModeCommands::MapModeCommands(EditorState& editor_state)
    : editor_state_(editor_state), frames_(1) {
  Add(L"?", NewHelpCommand(editor_state_, this, L"command mode"));
}

std::unique_ptr<MapModeCommands> MapModeCommands::NewChild() {
  auto output = std::make_unique<MapModeCommands>(editor_state_);
  output->frames_ = frames_;
  output->frames_.push_front({});

  // Override the parent's help command, so that bindings added to the child are
  // visible.
  output->Add(L"?",
              NewHelpCommand(editor_state_, output.get(), L"command mode"));
  return output;
}

std::map<wstring, std::map<wstring, Command*>> MapModeCommands::Coallesce()
    const {
  std::map<wstring, std::map<wstring, Command*>> output;
  std::set<wstring> already_seen;  // Avoid showing unreachable commands.
  for (const auto& frame : frames_) {
    for (const auto& it : frame->commands) {
      if (already_seen.insert(it.first).second) {
        output[it.second->Category()][it.first] = it.second.get();
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
                          NonNull<std::unique_ptr<Value>> value,
                          std::shared_ptr<vm::Environment> environment) {
  CHECK_EQ(value->type.type, VMType::FUNCTION);
  CHECK(value->type.type_arguments == std::vector<VMType>({VMType::Void()}));

  Add(name,
      MakeCommandFromFunction(
          std::bind_front(
              [&editor_state = editor_state_, environment](
                  NonNull<std::unique_ptr<vm::Expression>>& expression) {
                LOG(INFO) << "Evaluating expression from Value::Ptr...";
                Evaluate(*expression, environment,
                         [&editor_state](std::function<void()> callback) {
                           // TODO(easy, 2022-04-25): Schedule in Editor's work
                           // queue?
                           auto buffer = editor_state.current_buffer();
                           CHECK(buffer != nullptr);
                           buffer->work_queue()->Schedule(callback);
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

MapMode::MapMode(std::shared_ptr<MapModeCommands> commands)
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
