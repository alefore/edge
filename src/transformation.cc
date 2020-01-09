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

  futures::DelayedValue<Output> Apply(Input input) const override {
    const wstring& superfluous_characters = input.buffer->Read(
        buffer_variables::line_suffix_superfluous_characters);
    const auto line = input.buffer->LineAt(input.position.line);
    if (line == nullptr) return futures::ImmediateValue(Output());
    ColumnNumber column = line->EndColumn();
    while (column > ColumnNumber(0) &&
           superfluous_characters.find(
               line->get(column - ColumnNumberDelta(1))) != string::npos) {
      --column;
    }
    if (column == line->EndColumn()) return futures::ImmediateValue(Output());
    CHECK_LT(column, line->EndColumn());
    Output output = Output::SetColumn(column);

    DeleteOptions delete_options;
    delete_options.modifiers.repetitions =
        (line->EndColumn() - column).column_delta;
    delete_options.copy_to_paste_buffer = false;
    output.Push(NewDeleteTransformation(delete_options));
    return futures::ImmediateValue(std::move(output));
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

  futures::DelayedValue<Result> Apply(const Input& input) const override {
    CHECK(input.buffer != nullptr);
    struct Data {
      size_t index = 0;
      std::unique_ptr<Result> output;
    };
    auto data = std::make_shared<Data>();
    data->output = std::make_unique<Result>(input.position);
    return futures::ImmediateTransform(
        futures::While([this, data, input]() mutable {
          if (data->index == repetitions_) {
            return futures::ImmediateValue(
                futures::IterationControlCommand::kStop);
          }
          data->index++;
          Input current_input(input.buffer);
          current_input.mode = input.mode;
          current_input.position = data->output->position;
          return futures::ImmediateTransform(
              delegate_->Apply(current_input), [data](const Result& result) {
                bool made_progress = result.made_progress;
                data->output->MergeFrom(result);
                return made_progress && data->output->success
                           ? futures::IterationControlCommand::kContinue
                           : futures::IterationControlCommand::kStop;
              });
        }),
        [data](const futures::IterationControlCommand&) {
          return std::move(*data->output);
        });
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

Transformation::Result::Result(const Result& other)
    : success(other.success),
      made_progress(other.made_progress),
      modified_buffer(other.modified_buffer),
      undo_stack(other.undo_stack->CloneStack()),
      delete_buffer(other.delete_buffer),
      position(other.position) {}

Transformation::Result::Result(LineColumn position)
    : undo_stack(std::make_unique<TransformationStack>()), position(position) {}

Transformation::Result::Result(Result&&) = default;
Transformation::Result::~Result() = default;

void Transformation::Result::MergeFrom(const Result& sub_result) {
  success &= sub_result.success;
  made_progress |= sub_result.made_progress;
  modified_buffer |= sub_result.modified_buffer;
  undo_stack->PushFront(sub_result.undo_stack->Clone());
  if (sub_result.delete_buffer != nullptr) {
    delete_buffer = sub_result.delete_buffer;
  }
  position = sub_result.position;
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
