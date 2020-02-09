#ifndef __AFC_EDITOR_TRANSFORMATION_INSERT_H__
#define __AFC_EDITOR_TRANSFORMATION_INSERT_H__

#include <memory>

#include "src/transformation.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
struct InsertOptions {
  std::wstring Serialize() const;

  std::shared_ptr<const OpenBuffer> buffer_to_insert;

  Modifiers modifiers = Modifiers();

  enum class FinalPosition {
    // Leaves the buffer position at the start of the inserted text.
    kStart,

    // Leaves the buffer position at the end of the inserted text.
    kEnd,
  };
  // Ignored if `position` is set.
  FinalPosition final_position = FinalPosition::kEnd;

  std::optional<LineModifierSet> modifiers_set = std::nullopt;

  // If not present, will insert wherever the cursor is. If present, inserts the
  // text at this position.
  std::optional<LineColumn> position = std::nullopt;
};

std::unique_ptr<Transformation> NewInsertBufferTransformation(
    InsertOptions options);
void RegisterInsertTransformation(EditorState* editor,
                                  vm::Environment* environment);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_TRANSFORMATION_INSERT_H__
