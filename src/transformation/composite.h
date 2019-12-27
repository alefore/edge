#ifndef __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__
#define __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__

#include <memory>

#include "src/buffer_contents.h"
#include "src/transformation.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
// A particular type of transformation that doesn't directly modify the buffer
// but only does so indirectly, through other transformations (that it passes to
// Input::push).
//
// Ideally, most transformations will be expressed through this, so that we can
// isolate the lower-level primitive transformations.
class CompositeTransformation {
 public:
  virtual std::wstring Serialize() const = 0;

  struct Input {
    LineColumn original_position;
    // Adjusted to ensure that it is within the length of the current line.
    LineColumn position;
    Range range;
    EditorState* editor;
    const OpenBuffer* buffer;
    Modifiers modifiers;
    Transformation::Result::Mode mode;
    std::function<void(std::unique_ptr<Transformation> transformation)> push;
  };
  virtual void Apply(Input input) const = 0;
  virtual std::unique_ptr<CompositeTransformation> Clone() const = 0;
};

std::unique_ptr<Transformation> NewTransformation(
    Modifiers modifiers, std::unique_ptr<CompositeTransformation> composite);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__
