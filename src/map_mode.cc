#include "map_mode.h"
#include <memory>

#include <glog/logging.h>

#include "command.h"
#include "help_command.h"
#include "vm/public/types.h"
#include "vm/public/value.h"

namespace afc {
namespace editor {

using vm::VMType;
using vm::Value;

namespace {
class CommandFromFunction : public Command {
 public:
  CommandFromFunction(std::function<void()> callback, wstring description)
      : callback_(std::move(callback)), description_(std::move(description)) {}

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

MapMode::MapMode(std::shared_ptr<EditorMode> delegate)
    : delegate_(std::move(delegate)) {
  std::vector<const Map*> commands_vector;
  Populate(this, &commands_vector);
  Add(L"?",
      NewHelpCommand(std::move(commands_vector), L"command mode").release());
}

// static
void MapMode::Populate(const MapMode* input, std::vector<const Map*>* output) {
  while (input != nullptr) {
    output->push_back(&input->commands_);
    input = dynamic_cast<MapMode*>(input->delegate_.get());
  }
}

void MapMode::Add(wstring name, Command* value) {
  CHECK(value != nullptr);
  vector<wint_t> key;
  for (wchar_t c : name) {
    key.push_back(c);
  }
  commands_.insert({std::move(key), value});
}

void MapMode::Add(wstring name, std::unique_ptr<Value> value) {
  CHECK(value != nullptr);
  CHECK_EQ(value->type.type, VMType::FUNCTION);
  CHECK(value->type.type_arguments == std::vector<VMType>({ VMType::Void() }));
  auto callback = std::move(value->callback);
  // TODO: Don't leak it!
  Add(name, new CommandFromFunction([callback]() { callback({}); },
                                    L"C++ VM function"));
}

void MapMode::Add(wstring name, std::function<void()> callback,
                  wstring description) {
  // TODO: Don't leak it!
  Add(name, new CommandFromFunction(std::move(callback), description));
}

void MapMode::ProcessInput(wint_t c, EditorState* editor_state) {
  vector<wint_t> input = current_input_;
  input.push_back(c);
  auto it = commands_.lower_bound(input);
  if (it == commands_.end()
      || !std::equal(input.begin(), input.end(), it->first.begin())) {
    current_input_ = {};
    if (delegate_ != nullptr) {
      for (wint_t c : input) {
        delegate_->ProcessInput(c, editor_state);
      }
    }
    return;
  }
  if (input != it->first) {
    current_input_ = std::move(input);
    return;
  }

  current_input_ = {};
  CHECK(it->second);
  it->second->ProcessInput(c, editor_state);
}

}  // namespace editor
}  // namespace afc
