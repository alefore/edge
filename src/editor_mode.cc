#include "src/editor_mode.h"

namespace afc::editor {
size_t EditorMode::Receive(
    const std::vector<infrastructure::ExtendedChar>& input,
    size_t start_index) {
  CHECK_LT(start_index, input.size());
  ProcessInput(input.at(start_index));
  return 1;
}
}  // namespace afc::editor
