#ifndef __AFC_EDITOR_TRANSFORMATION_INPUT_H__
#define __AFC_EDITOR_TRANSFORMATION_INPUT_H__

#include "src/line_column.h"

namespace afc::editor {
class OpenBuffer;
class CompositeTransformation;
namespace transformation {
struct Input {
  explicit Input(editor::OpenBuffer* buffer);
  Input NewChild(LineColumn position) const;

  // Input parameter.
  enum class Mode {
    // Just preview what this transformation would do. Don't apply any
    // long-lasting effects.
    kPreview,
    // Apply the transformation.
    kFinal,
  };
  // Input parameter.
  Mode mode = Mode::kFinal;

  // The buffer that the transformation should modify.
  editor::OpenBuffer* const buffer;

  // If non-null, if the transformation deletes text, it should append it to
  // this buffer (for pasting it later).
  editor::OpenBuffer* delete_buffer = nullptr;

  // Where should the transformation be applied?
  editor::LineColumn position;
};
}  // namespace transformation
}  // namespace afc::editor
#endif  // __AFC_EDITOR_TRANSFORMATION_INPUT_H__
