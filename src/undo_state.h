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

  void CommitCurrent();
  void AbandonCurrent();
  language::NonNull<std::shared_ptr<transformation::Stack>> Current();
  void SetCurrentModifiedBuffer();

  size_t UndoStackSize() const;
  size_t RedoStackSize() const;

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

    enum class RedoMode { kIgnore, kPopulate };
    RedoMode redo_mode;
    Direction direction;
    size_t repetitions;
    std::function<futures::Value<transformation::Result>(
        transformation::Variant)>
        callback;
  };
  futures::Value<language::EmptyValue> Apply(ApplyOptions apply_options);

 private:
  // undo_stack_ contains a list of "undo" transformations for all changes to
  // the buffer. The last entry corresponds to the last transformation to the
  // buffer.
  std::list<language::NonNull<std::shared_ptr<transformation::Stack>>>
      undo_stack_;

  struct RedoStackEntry {
    // The original entry from undo_stack_.
    language::NonNull<std::shared_ptr<transformation::Stack>> undo;

    // A transformation that results in undoing the undo transformation.
    language::NonNull<std::shared_ptr<transformation::Stack>> redo;
  };

  // When we are applying undo transformations, we push into redo_stack_.
  std::list<RedoStackEntry> redo_stack_;

  // As we go applying a set of transformations that should be undone
  // atomically,, we start pushing into current_ their corresponding "undo"
  // transformations. Once the atom is commited, we move from current_ into
  // past_.
  language::NonNull<std::shared_ptr<transformation::Stack>> current_;
  bool current_modified_buffer_ = false;
};
}  // namespace afc::editor
