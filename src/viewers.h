#ifndef __AFC_EDITOR_VIEWERS_H__
#define __AFC_EDITOR_VIEWERS_H__

#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <set>

#include "src/line_column.h"
#include "src/observers.h"

namespace afc {
namespace editor {

// Keeps track of the view size of the last active viewer (last caller to
// `set_view_size`), allowing the buffer to inspect that. When the view size
// changes, notifies any registered listeners.
//
// Not thread safe.
class Viewers {
 public:
  using Registration = std::unique_ptr<bool, std::function<void(bool*)>>;

  // Widgets should call this when they first start displaying a buffer or when
  // they deliver input to that buffer.
  void set_view_size(LineColumnDelta view_size);

  // Adds a callback that will be updated whenever the view size changes. Once
  // the first such callback runs, view_size_ will always have a value.
  void AddObserver(Observers::Observer observer);

  std::optional<LineColumnDelta> view_size() const;

 private:
  std::optional<LineColumnDelta> view_size_;

  Observers observers_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_VIEWERS_H__
