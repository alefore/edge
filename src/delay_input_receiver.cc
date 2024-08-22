#include "src/delay_input_receiver.h"

using afc::infrastructure::ExtendedChar;

namespace afc::editor {
DelayInputReceiver::DelayInputReceiver(CursorMode cursor_mode)
    : cursor_mode_(cursor_mode) {}

void DelayInputReceiver::ProcessInput(ExtendedChar c) { input_.push_back(c); }

EditorMode::CursorMode DelayInputReceiver::cursor_mode() const {
  return cursor_mode_;
}

const std::vector<ExtendedChar>& DelayInputReceiver::input() const {
  return input_;
}
}  // namespace afc::editor
