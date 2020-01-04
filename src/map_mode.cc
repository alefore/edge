#include "src/map_mode.h"

#include <glog/logging.h>

#include <memory>
#include <set>

#include "src/command.h"
#include "src/editor.h"
#include "src/help_command.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc {
namespace editor {

using vm::Expression;
using vm::Value;
using vm::VMType;

namespace {
class CommandFromFunction : public Command {
 public:
  CommandFromFunction(std::function<void(EditorState*)> callback,
                      wstring description)
      : callback_(std::move(callback)), description_(std::move(description)) {
    CHECK(callback_ != nullptr);
  }

  std::wstring Description() const override { return description_; }
  std::wstring Category() const override {
    return L"C++ Functions (Extensions)";
  }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    callback_(editor_state);
  }

 private:
  const std::function<void(EditorState*)> callback_;
  const wstring description_;
};

}  // namespace

class EditorState;
MapModeCommands::MapModeCommands() : frames_({std::make_shared<Frame>()}) {
  Add(L"?", NewHelpCommand(this, L"command mode"));
}

std::unique_ptr<MapModeCommands> MapModeCommands::NewChild() {
  auto output = std::make_unique<MapModeCommands>();
  output->frames_ = frames_;
  output->frames_.push_front(std::make_shared<Frame>());

  // Override the parent's help command, so that bindings added to the child are
  // visible.
  output->Add(L"?", NewHelpCommand(output.get(), L"command mode"));
  return std::move(output);
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

void MapModeCommands::Add(wstring name, std::unique_ptr<Command> value) {
  CHECK(value != nullptr);
  CHECK(!frames_.empty());
  frames_.front()->commands.insert({name, std::move(value)});
}

void MapModeCommands::Add(wstring name, wstring description,
                          std::unique_ptr<Value> value,
                          std::shared_ptr<vm::Environment> environment) {
  CHECK(value != nullptr);
  CHECK_EQ(value->type.type, VMType::FUNCTION);
  CHECK(value->type.type_arguments == std::vector<VMType>({VMType::Void()}));
  // TODO: Make a unique_ptr (once capture of unique_ptr is feasible).
  std::shared_ptr<vm::Expression> expression =
      NewFunctionCall(NewConstantExpression(std::move(value)), {});
  Add(name,
      std::make_unique<CommandFromFunction>(
          [expression, environment](EditorState* editor_state) {
            LOG(INFO) << "Evaluating expression from Value::Ptr...";
            Evaluate(
                expression.get(), environment,
                [expression](Value::Ptr) { LOG(INFO) << "Done evaluating."; },
                [editor_state](std::function<void()> callback) {
                  auto buffer = editor_state->current_buffer();
                  CHECK(buffer != nullptr);
                  buffer->work_queue()->Schedule(callback);
                });
          },
          description));
}

void MapModeCommands::Add(wstring name,
                          std::function<void(EditorState*)> callback,
                          wstring description) {
  Add(name, std::make_unique<CommandFromFunction>(std::move(callback),
                                                  std::move(description)));
}

void MapModeCommands::RegisterVariableCommand(wstring variable_name,
                                              wstring command_name) {
  CHECK(!frames_.empty());
  frames_.front()->variable_commands[variable_name].insert(command_name);
}

std::map<std::wstring, std::set<std::wstring>>
MapModeCommands::GetVariableCommands() const {
  std::map<std::wstring, std::set<std::wstring>> output;
  std::set<wstring> already_seen;  // Avoid showing unreachable commands.
  for (const auto& frame : frames_) {
    for (const auto& variable_it : frame->variable_commands) {
      wstring variable_name = variable_it.first;
      for (const wstring& command_name : variable_it.second) {
        if (already_seen.insert(command_name).second) {
          output[variable_name].insert(command_name);
        }
      }
    }
  }
  return output;
}

MapMode::MapMode(std::shared_ptr<MapModeCommands> commands)
    : commands_(std::move(commands)) {}

void MapMode::ProcessInput(wint_t c, EditorState* editor_state) {
  current_input_.push_back(c);

  bool reset_input = true;
  for (const auto& frame : commands_->frames_) {
    auto it = frame->commands.lower_bound(current_input_);
    if (it != frame->commands.end() &&
        std::equal(current_input_.begin(), current_input_.end(),
                   it->first.begin())) {
      if (current_input_ == it->first) {
        CHECK(it->second);
        current_input_ = L"";
        it->second->ProcessInput(c, editor_state);
        return;
      }
      reset_input = false;
    }
  }

  if (reset_input) {
    current_input_ = L"";
  }
}

}  // namespace editor
}  // namespace afc
