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

class ApplyRepetitionsTransformation : public Transformation {
 public:
  ApplyRepetitionsTransformation(int repetitions,
                                 unique_ptr<Transformation> delegate)
      : repetitions_(repetitions), delegate_(std::move(delegate)) {}

  futures::Value<Result> Apply(const Input& input) const override {
    CHECK(input.buffer != nullptr);
    struct Data {
      size_t index = 0;
      std::unique_ptr<Result> output;
    };
    auto data = std::make_shared<Data>();
    data->output = std::make_unique<Result>(input.position);
    return futures::Transform(
        futures::While([this, data, input]() mutable {
          if (data->index == repetitions_) {
            return futures::Past(futures::IterationControlCommand::kStop);
          }
          data->index++;
          Input current_input(input.buffer);
          current_input.mode = input.mode;
          current_input.position = data->output->position;
          return futures::Transform(
              delegate_->Apply(current_input), [data](Result result) {
                bool made_progress = result.made_progress;
                data->output->MergeFrom(std::move(result));
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

std::unique_ptr<Transformation> NewApplyRepetitionsTransformation(
    size_t repetitions, unique_ptr<Transformation> transformation) {
  return std::make_unique<ApplyRepetitionsTransformation>(
      repetitions, std::move(transformation));
}
}  // namespace afc::editor
