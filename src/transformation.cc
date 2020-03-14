#include "src/transformation.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"

namespace afc::editor {
namespace {
class DeleteSuffixSuperfluousCharacters : public CompositeTransformation {
 public:
  std::wstring Serialize() const override {
    return L"DeleteSuffixSuperfluousCharacters()";
  }

  futures::Value<Output> Apply(Input input) const override {
    const wstring& superfluous_characters = input.buffer->Read(
        buffer_variables::line_suffix_superfluous_characters);
    const auto line = input.buffer->LineAt(input.position.line);
    if (line == nullptr) return futures::Past(Output());
    ColumnNumber column = line->EndColumn();
    while (column > ColumnNumber(0) &&
           superfluous_characters.find(
               line->get(column - ColumnNumberDelta(1))) != string::npos) {
      --column;
    }
    if (column == line->EndColumn()) return futures::Past(Output());
    CHECK_LT(column, line->EndColumn());
    Output output = Output::SetColumn(column);

    transformation::Delete delete_options;
    delete_options.modifiers.repetitions =
        (line->EndColumn() - column).column_delta;
    delete_options.modifiers.paste_buffer_behavior =
        Modifiers::PasteBufferBehavior::kDoNothing;
    output.Push(delete_options);
    return futures::Past(std::move(output));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<DeleteSuffixSuperfluousCharacters>();
  }
};

;
}  // namespace

Transformation::Input::Input(OpenBuffer* buffer) : buffer(buffer) {}

Transformation::Result::Result(LineColumn position)
    : undo_stack(std::make_unique<TransformationStack>()), position(position) {}

Transformation::Result::Result(Result&&) = default;
Transformation::Result::~Result() = default;

void Transformation::Result::MergeFrom(Result sub_result) {
  success &= sub_result.success;
  made_progress |= sub_result.made_progress;
  modified_buffer |= sub_result.modified_buffer;
  undo_stack->PushFront(std::move(sub_result.undo_stack->Build()));
  if (sub_result.delete_buffer != nullptr) {
    delete_buffer = std::move(sub_result.delete_buffer);
  }
  position = sub_result.position;
}

unique_ptr<Transformation> TransformationAtPosition(
    const LineColumn& position, unique_ptr<Transformation> transformation) {
  return ComposeTransformation(
      transformation::Build(transformation::SetPosition(position)),
      std::move(transformation));
}

std::unique_ptr<Transformation> NewDeleteSuffixSuperfluousCharacters() {
  return NewTransformation(
      Modifiers(), std::make_unique<DeleteSuffixSuperfluousCharacters>());
}
}  // namespace afc::editor
