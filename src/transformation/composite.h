#ifndef __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__
#define __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__

#include <memory>

#include "src/buffer_contents.h"
#include "src/futures/futures.h"
#include "src/transformation.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
namespace transformation {
class Stack;
}

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
    Transformation::Input::Mode mode;
  };

  class Output {
   public:
    static Output SetPosition(LineColumn position);
    static Output SetColumn(ColumnNumber column);
    Output();
    Output(Output&&);
    Output(std::unique_ptr<Transformation> transformation);
    void Push(std::unique_ptr<Transformation> transformation);
    void Push(transformation::BaseTransformation base_transformation);

    std::unique_ptr<transformation::Stack> stack;
  };
  virtual futures::Value<Output> Apply(Input input) const = 0;
  virtual std::unique_ptr<CompositeTransformation> Clone() const = 0;
};

void RegisterCompositeTransformation(vm::Environment* environment);
namespace transformation {
struct ModifiersAndComposite {
  Modifiers modifiers = Modifiers();
  std::shared_ptr<CompositeTransformation> transformation;
};

futures::Value<Transformation::Result> ApplyBase(
    const ModifiersAndComposite& parameters, Transformation::Input input);
futures::Value<Transformation::Result> ApplyBase(
    const std::shared_ptr<CompositeTransformation>& parameters,
    Transformation::Input input);

}  // namespace transformation
}  // namespace afc::editor
namespace afc::vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Output>> {
  static std::shared_ptr<editor::CompositeTransformation::Output> get(
      Value* value);
  static Value::Ptr New(
      std::shared_ptr<editor::CompositeTransformation::Output> value);
  static const VMType vmtype;
};
template <>
struct VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Input>> {
  static std::shared_ptr<editor::CompositeTransformation::Input> get(
      Value* value);
  static Value::Ptr New(
      std::shared_ptr<editor::CompositeTransformation::Input> value);
  static const VMType vmtype;
};
}  // namespace afc::vm
#endif  // __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__
