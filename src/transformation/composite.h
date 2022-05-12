#ifndef __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__
#define __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__

#include <memory>

#include "src/buffer_contents.h"
#include "src/futures/futures.h"
#include "src/transformation.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm/public/environment.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::editor {
namespace transformation {
class Stack;
}

// A particular type of transformation that doesn't directly modify the buffer
// but only does so indirectly, through other transformations (that it passes to
// Output::push).
//
// Ideally, most transformations will be expressed through this, so that we can
// isolate the lower-level primitive transformations.
class CompositeTransformation {
 public:
  virtual std::wstring Serialize() const = 0;

  struct Input {
    EditorState& editor;
    LineColumn original_position = LineColumn();
    // Adjusted to ensure that it is within the length of the current line.
    LineColumn position = LineColumn();
    Range range = Range();
    const OpenBuffer& buffer;
    Modifiers modifiers = Modifiers();
    transformation::Input::Mode mode = transformation::Input::Mode::kFinal;
  };

  class Output {
   public:
    static Output SetPosition(LineColumn position);
    static Output SetColumn(ColumnNumber column);
    Output();
    Output(Output&&);
    Output(transformation::Variant transformation);
    void Push(transformation::Variant transformation);

    std::unique_ptr<transformation::Stack> stack;
  };
  virtual futures::Value<Output> Apply(Input input) const = 0;
};

void RegisterCompositeTransformation(language::gc::Pool& pool,
                                     vm::Environment* environment);
namespace transformation {
struct ModifiersAndComposite {
  Modifiers modifiers = Modifiers();
  language::NonNull<std::shared_ptr<CompositeTransformation>> transformation;
};

futures::Value<Result> ApplyBase(const ModifiersAndComposite& parameters,
                                 Input input);
futures::Value<Result> ApplyBase(
    const language::NonNull<std::shared_ptr<CompositeTransformation>>&
        parameters,
    Input input);
std::wstring ToStringBase(const ModifiersAndComposite& parameters);
std::wstring ToStringBase(
    const language::NonNull<std::shared_ptr<CompositeTransformation>>&
        parameters);

Variant OptimizeBase(ModifiersAndComposite transformation);
Variant OptimizeBase(
    const language::NonNull<std::shared_ptr<CompositeTransformation>>&
        transformation);
}  // namespace transformation
}  // namespace afc::editor
namespace afc::vm {
template <>
struct VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Output>> {
  static std::shared_ptr<editor::CompositeTransformation::Output> get(
      Value& value);
  static language::NonNull<Value::Ptr> New(
      language::gc::Pool& pool,
      std::shared_ptr<editor::CompositeTransformation::Output> value);
  static const VMType vmtype;
};
template <>
struct VMTypeMapper<std::shared_ptr<editor::CompositeTransformation::Input>> {
  static std::shared_ptr<editor::CompositeTransformation::Input> get(
      Value& value);
  static language::NonNull<Value::Ptr> New(
      language::gc::Pool& pool,
      std::shared_ptr<editor::CompositeTransformation::Input> value);
  static const VMType vmtype;
};
}  // namespace afc::vm
#endif  // __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__
