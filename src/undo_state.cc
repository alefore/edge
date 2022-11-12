#include "src/undo_state.h"

namespace afc::editor {

using futures::IterationControlCommand;
using language::EmptyValue;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using transformation::Variant;

void UndoState::Clear() {
  past_.clear();
  future_.clear();
}

void UndoState::StartNewStep() {
  future_.clear();
  past_.push_back(MakeNonNullUnique<transformation::Stack>());
}

NonNull<std::shared_ptr<transformation::Stack>> UndoState::GetLastStep() {
  if (past_.empty()) {
    VLOG(5) << "Attempted to get last undo past which ... was empty.";
    past_.push_back(MakeNonNullUnique<transformation::Stack>());
  }
  return past_.back();
}

futures::Value<EmptyValue> UndoState::Apply(ApplyOptions apply_options) {
  struct Data {
    std::list<NonNull<std::shared_ptr<transformation::Stack>>>* source;
    std::list<NonNull<std::shared_ptr<transformation::Stack>>>* target;
    size_t repetitions = 0;
    ApplyOptions options;
  };
  auto data = std::make_shared<Data>();
  if (apply_options.direction == Direction::kForwards) {
    data->source = &past_;
    data->target = &future_;
  } else {
    data->source = &future_;
    data->target = &past_;
  }
  data->options = std::move(apply_options);
  return futures::While([this, data] {
           if (data->repetitions == data->options.repetitions ||
               data->source->empty()) {
             return futures::Past(IterationControlCommand::kStop);
           }

           transformation::Variant value =
               std::move(data->source->back().value());
           data->source->pop_back();

           return data->options.callback(value).Transform(
               [data](transformation::Result result) {
                 if (result.modified_buffer ||
                     data->options.mode == ApplyOptions::Mode::kOnlyOne) {
                   data->repetitions++;
                 }
                 data->target->push_back(std::move(result.undo_stack));
                 return IterationControlCommand::kContinue;
               });
           ;
         })
      .Transform([](IterationControlCommand) { return EmptyValue(); });
}

}  // namespace afc::editor
