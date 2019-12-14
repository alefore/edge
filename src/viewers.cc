#include "src/viewers.h"

#include "src/wstring.h"

namespace afc {
namespace editor {

void Viewers::set_view_size(LineColumnDelta view_size) {
  if (view_size == view_size_) return;  // Optimization.
  view_size_ = view_size;
  for (auto& l : listeners_) {
    l();
  }
}

Viewers::Registration Viewers::AddListener(std::function<void()> listener) {
  CHECK_LT(listeners_.size(), 10000);
  listeners_.push_front(listener);
  return std::unique_ptr<bool, std::function<void(bool*)>>(
      new bool(), [this, it = listeners_.begin()](bool* value) {
        delete value;
        listeners_.erase(it);
      });
}

std::optional<LineColumnDelta> Viewers::view_size() const { return view_size_; }

}  // namespace editor
}  // namespace afc
