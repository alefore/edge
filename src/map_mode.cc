#include "map_mode.h"
#include <memory>

#include <glog/logging.h>

#include "command.h"
#include "help_command.h"
#include "vm/public/constant_expression.h"
#include "vm/public/types.h"
#include "vm/public/value.h"
#include "vm/public/function_call.h"
#include "vm/public/vm.h"

namespace afc {
namespace editor {

using vm::VMType;
using vm::Value;
using vm::Expression;

namespace {
class CommandFromFunction : public Command {
 public:
  CommandFromFunction(std::function<void()> callback, wstring description)
      : callback_(std::move(callback)), description_(std::move(description)) {
    CHECK(callback_ != nullptr);
  }

  const std::wstring Description() override {
    return description_;
  }

  void ProcessInput(wint_t, EditorState*) override {
    callback_();
  }

 private:
  const std::function<void()> callback_;
  const wstring description_;
};

}  // namespace

struct EditorState;
MapModeCommands::MapModeCommands()
    : commands_({std::make_shared<map<wstring, Command*>>()}) {
  Add(L"?", NewHelpCommand(this, L"command mode").release());
}

std::unique_ptr<MapModeCommands> MapModeCommands::NewChild() {
  std::unique_ptr<MapModeCommands> output(new MapModeCommands());
  output->commands_ = commands_;
  output->commands_.push_front(std::make_shared<map<wstring, Command*>>());
  return std::move(output);
}

std::map<wstring, Command*> MapModeCommands::Coallesce() const {
  std::map<wstring, Command*> output;
  for (const auto& node : commands_) {
    for (const auto& it : *node) {
      if (output.count(it.first) == 0) {
        output.insert({it.first, it.second});
      }
    }
  }
  return output;
}

void MapModeCommands::Add(wstring name, Command* value) {
  CHECK(value != nullptr);
  commands_.front()->insert({name, value});
}

void MapModeCommands::Add(wstring name, std::unique_ptr<Value> value) {
  CHECK(value != nullptr);
  CHECK_EQ(value->type.type, VMType::FUNCTION);
  CHECK(value->type.type_arguments == std::vector<VMType>({ VMType::Void() }));
  std::shared_ptr<vm::Expression> expression = NewFunctionCall(
      NewConstantExpression(std::move(value)),
      unique_ptr<vector<unique_ptr<vm::Expression>>>(
          new vector<unique_ptr<Expression>>()));
  // TODO: Don't leak it!
  Add(name,
      new CommandFromFunction(
          [expression]() {
            LOG(INFO) << "Evaluating expression...";
            Evaluate(expression.get(),
                     nullptr,
                     [](Value::Ptr) { LOG(INFO) << "Done evaluating."; });
          },
          L"C++ VM function"));
}

void MapModeCommands::Add(wstring name, std::function<void()> callback,
                          wstring description) {
  // TODO: Don't leak it!
  Add(name, new CommandFromFunction(std::move(callback), description));
}

MapMode::MapMode(std::shared_ptr<MapModeCommands> commands)
    : commands_(std::move(commands)) {}


void MapMode::ProcessInput(wint_t c, EditorState* editor_state) {
  current_input_.push_back(c);

  bool reset_input = true;
  for (const auto& node : commands_->commands_) {
    auto it = node->lower_bound(current_input_);
    if (it != node->end()
        && std::equal(current_input_.begin(), current_input_.end(),
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
