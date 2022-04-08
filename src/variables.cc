#include "src/variables.h"

namespace afc::editor {
void RunObservers(std::vector<VariableObserver>& observers) {
  for (auto& o : observers) o();
}
}  // namespace afc::editor
