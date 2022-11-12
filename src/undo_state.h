#include <list>
#include <memory>

#include "src/futures/futures.h"
#include "src/language/safe_types.h"
#include "src/transformation/stack.h"

namespace afc::editor {
class UndoState {
 public:
  UndoState() = default;

  void Clear();
  void StartNewStep();
  language::NonNull<std::shared_ptr<transformation::Stack>> GetLastStep();

  struct ApplyOptions {
    enum class Mode {
      // Iterate the history, undoing transformations, until the buffer is
      // actually modified.
      kLoop,
      // Only undo the last transformation (whether or not that causes any
      // modifications).
      kOnlyOne
    };

    Mode mode;
    Direction direction;
    size_t repetitions;
    std::function<futures::Value<transformation::Result>(
        transformation::Variant&)>
        callback;
  };
  futures::Value<language::EmptyValue> Apply(ApplyOptions apply_options);

 private:
  // When a transformation is done, we append its result to undo_past_, so that
  // it can be undone.
  std::list<language::NonNull<std::shared_ptr<transformation::Stack>>> past_;
  std::list<language::NonNull<std::shared_ptr<transformation::Stack>>> future_;
};
}  // namespace afc::editor
