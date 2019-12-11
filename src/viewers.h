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

  // When a widget with a given display size wants to register interest in a
  // buffer, it should call `Register`. It should hold the returned object
  // until it's done (either a new size is requested, or it switches to a new
  // buffer).
  Registration Register(LineColumnDelta view_size);

  // Adds a callback that will be updated whenever last_view_size_ changes.
  // The callback will be executed until the time when the customer deletes the
  // returned value. Once the first such callback runs, last_view_size_ will
  // always have a value.
  Registration AddListener(std::function<void()> listener);

  std::set<LineColumnDelta> GetViewSizes() const;

  std::optional<LineColumnDelta> last_view_size() const;

 private:
  // Holds the last value added to view_sizes_ when the same value didn't
  // already exist there. This is a good heuristic to try to select the last
  // active screen.
  //
  // TODO: Instead of this, use the last one that has had input.
  std::optional<LineColumnDelta> last_view_size_;
  // We use a `shared_ptr` simply to allow `Viewers` to be deleted (which
  // happens when its containing `OpenBuffer` gets deleted) while pointers
  // returned by `Register` are still alive (because some widget hasn't
  // detected that the buffer was deleted). In that case, when the pointer
  // returned by `Register` is finally deleted, its custom deleter will
  // detect that the underlying structure is gone.
  const std::shared_ptr<std::multiset<LineColumnDelta>> view_sizes_ =
      std::make_shared<std::multiset<LineColumnDelta>>();
  std::list<std::function<void()>> listeners_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_VIEWERS_H__
