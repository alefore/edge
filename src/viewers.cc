#include "src/viewers.h"

#include "src/wstring.h"

namespace afc {
namespace editor {

Viewers::Registration Viewers::Register(LineColumnDelta view_size) {
  // TODO: Optimize to not require 2 tree traversals?
  CHECK_LT(view_sizes_.size(), 10000);
  bool new_item = view_sizes_.find(view_size) == view_sizes_.end();
  view_sizes_.insert(view_size);
  if (new_item) {
    last_view_size_ = view_size;
    for (auto& l : listeners_) {
      l();
    }
  }
  return std::unique_ptr<bool, std::function<void(bool*)>>(
      new bool(), [this, view_size](bool* value) {
        delete value;
        auto it = view_sizes_.find(view_size);
        CHECK(it != view_sizes_.end());
        view_sizes_.erase(it);
      });
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

std::set<LineColumnDelta> Viewers::GetViewSizes() const {
  return std::set<LineColumnDelta>(view_sizes_.begin(), view_sizes_.end());
}

std::optional<LineColumnDelta> Viewers::last_view_size() const {
  return last_view_size_;
}

}  // namespace editor
}  // namespace afc
