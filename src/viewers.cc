#include "src/viewers.h"

#include "src/wstring.h"

namespace afc {
namespace editor {

void Viewers::set_view_size(LineColumnDelta view_size) {
  if (view_size == view_size_) return;  // Optimization.
  view_size_ = view_size;
  observers_.Notify();
}

void Viewers::AddObserver(Observers::Observer observer) {
  observers_.Add(std::move(observer));
}

std::optional<LineColumnDelta> Viewers::view_size() const { return view_size_; }

}  // namespace editor
}  // namespace afc
