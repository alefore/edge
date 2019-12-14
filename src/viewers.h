#ifndef __AFC_EDITOR_VIEWERS_H__
#define __AFC_EDITOR_VIEWERS_H__

#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <set>

#include "src/line_column.h"

namespace afc {
namespace editor {

// Not thread safe.
class Viewers {
 public:
  using Registration = std::unique_ptr<bool, std::function<void(bool*)>>;

  // Widgets should call this when they first start displaying a buffer or when
  // they deliver input to that buffer.
  void set_view_size(LineColumnDelta view_size);

  // Adds a callback that will be updated whenever the view size changes. The
  // callback will be executed until the time when the customer deletes the
  // returned value. Once the first such callback runs, view_size_ will always
  // have a value.
  Registration AddListener(std::function<void()> listener);

  std::optional<LineColumnDelta> view_size() const;

 private:
  std::optional<LineColumnDelta> view_size_;

  std::list<std::function<void()>> listeners_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_VIEWERS_H__
