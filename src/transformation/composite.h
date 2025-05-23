#ifndef __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__
#define __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__

#include <memory>

#include "src/futures/futures.h"
#include "src/language/text/line_sequence.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/transformation/variant.h"
#include "src/vm/environment.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::editor {
namespace transformation {
struct Stack;
}

// A particular type of transformation that doesn't directly modify the buffer
// but only does so indirectly, through other transformations (that it passes to
// Output::push).
//
// Ideally, most transformations will be expressed through this, so that we can
// isolate the lower-level primitive transformations.
class CompositeTransformation {
 public:
  virtual ~CompositeTransformation() = default;

  virtual std::wstring Serialize() const = 0;

  struct Input {
    EditorState& editor;
    language::text::LineColumn original_position = language::text::LineColumn();
    // Adjusted to ensure that it is within the length of the current line.
    language::text::LineColumn position = language::text::LineColumn();
    language::text::Range range = language::text::Range();
    const OpenBuffer& buffer;
    Modifiers modifiers = Modifiers();
    transformation::Input::Mode mode = transformation::Input::Mode::kFinal;
  };

  class Output {
   public:
    static Output SetPosition(language::text::LineColumn position);
    static Output SetColumn(language::lazy_string::ColumnNumber column);
    Output() = default;
    Output(Output&&);
    Output(transformation::Variant transformation);
    void Push(transformation::Variant transformation);

    transformation::Stack stack;
  };
  virtual futures::Value<Output> Apply(Input input) const = 0;
};

void RegisterCompositeTransformation(language::gc::Pool& pool,
                                     vm::Environment& environment);
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
#endif  // __AFC_EDITOR_TRANSFORMATION_COMPOSITE_H__
