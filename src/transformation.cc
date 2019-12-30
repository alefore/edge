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

namespace afc::editor {
namespace {
class DeleteSuffixSuperfluousCharacters : public CompositeTransformation {
 public:
  std::wstring Serialize() const override {
    return L"DeleteSuffixSuperfluousCharacters()";
  }

  void Apply(Input input) const override {
    const wstring& superfluous_characters = input.buffer->Read(
        buffer_variables::line_suffix_superfluous_characters);
    const auto line = input.buffer->LineAt(input.position.line);
    if (line == nullptr) return;
    ColumnNumber column = line->EndColumn();
    while (column > ColumnNumber(0) &&
           superfluous_characters.find(
               line->get(column - ColumnNumberDelta(1))) != string::npos) {
      --column;
    }
    if (column == line->EndColumn()) return;
    CHECK_LT(column, line->EndColumn());
    input.push(NewSetPositionTransformation(std::nullopt, column));

    DeleteOptions delete_options;
    delete_options.modifiers.repetitions =
        (line->EndColumn() - column).column_delta;
    delete_options.copy_to_paste_buffer = false;
    input.push(NewDeleteTransformation(delete_options));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<DeleteSuffixSuperfluousCharacters>();
  }
};

class ApplyRepetitionsTransformation : public Transformation {
 public:
  ApplyRepetitionsTransformation(int repetitions,
                                 unique_ptr<Transformation> delegate)
      : repetitions_(repetitions), delegate_(std::move(delegate)) {}

  Result Apply(const Input& input) const override {
    CHECK(input.buffer != nullptr);
    Result output(input.position);
    for (size_t i = 0; i < repetitions_; i++) {
      Input current_input(input.buffer);
      current_input.mode = input.mode;
      current_input.position = output.position;
      auto result = delegate_->Apply(current_input);
      bool made_progress = result.made_progress;
      output.MergeFrom(std::move(result));
      if (!made_progress) {
        LOG(INFO) << "Application " << i << " didn't make progress, giving up.";
        break;
      }
      if (!output.success) {
        LOG(INFO) << "Application " << i << " didn't succeed, giving up.";
        break;
      }
    }
    return output;
  }

  unique_ptr<Transformation> Clone() const override {
    return NewApplyRepetitionsTransformation(repetitions_, delegate_->Clone());
  }

 private:
  const size_t repetitions_;
  const std::unique_ptr<Transformation> delegate_;
};
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
  undo_stack->PushFront(std::move(sub_result.undo_stack));
  if (sub_result.delete_buffer != nullptr) {
    delete_buffer = std::move(sub_result.delete_buffer);
  }
  position = std::move(sub_result.position);
}

unique_ptr<Transformation> TransformationAtPosition(
    const LineColumn& position, unique_ptr<Transformation> transformation) {
  return ComposeTransformation(NewSetPositionTransformation(position),
                               std::move(transformation));
}

std::unique_ptr<Transformation> NewDeleteSuffixSuperfluousCharacters() {
  return NewTransformation(
      Modifiers(), std::make_unique<DeleteSuffixSuperfluousCharacters>());
}

std::unique_ptr<Transformation> NewApplyRepetitionsTransformation(
    size_t repetitions, unique_ptr<Transformation> transformation) {
  return std::make_unique<ApplyRepetitionsTransformation>(
      repetitions, std::move(transformation));
}
}  // namespace afc::editor
