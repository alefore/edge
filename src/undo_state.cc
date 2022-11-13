#include "src/undo_state.h"

namespace afc::editor {

using futures::IterationControlCommand;
using language::EmptyValue;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using transformation::Variant;

void UndoState::Clear() {
  AbandonCurrent();
  undo_stack_.clear();
  redo_stack_.clear();
}

void UndoState::CommitCurrent() {
  if (redo_stack_.empty() && current_->stack.empty()) return;
  if (current_modified_buffer_) {
    // Suppose this history:
    //
    //   v1 -a-> v2 -b-> v3 -c-> v4
    //
    // If we undo to v2 and apply change d to v5, we want the history to be:
    //
    //   v1 -a-> v2 -b-> v3 -c-> v4 -c'-> v3 -b'-> v2 -d-> v5
    //
    // As change d is commited and we get here, we start with:
    //
    //   undo_stack_: a
    //   redo_stack_: c, b
    std::list<NonNull<std::shared_ptr<transformation::Stack>>> undo_chain;
    std::list<NonNull<std::shared_ptr<transformation::Stack>>> redo_chain;
    for (auto& entry : redo_stack_) {
      undo_chain.push_front(std::move(entry.undo));
      redo_chain.push_back(std::move(entry.redo));
    }
    redo_stack_.clear();

    // We have transfered from redo_stack_ into undo_chain and redo_chain:
    //
    //   undo_chain: b, c
    //   redo_chain: c', b'
    //
    // Now we insert them into undo_stack_.
    undo_stack_.splice(undo_stack_.end(), undo_chain);
    undo_stack_.splice(undo_stack_.end(), redo_chain);
  }
  undo_stack_.push_back(std::move(current_));
  AbandonCurrent();
}

void UndoState::AbandonCurrent() {
  current_ = MakeNonNullUnique<transformation::Stack>();
  current_modified_buffer_ = false;
}

NonNull<std::shared_ptr<transformation::Stack>> UndoState::Current() {
  return current_;
}

void UndoState::SetCurrentModifiedBuffer() { current_modified_buffer_ = true; }

size_t UndoState::UndoStackSize() const { return undo_stack_.size(); }
size_t UndoState::RedoStackSize() const { return redo_stack_.size(); }

futures::Value<EmptyValue> UndoState::Apply(ApplyOptions apply_options) {
  struct Data {
    size_t repetitions = 0;
    ApplyOptions options;
  };
  auto data = std::make_shared<Data>();
  data->options = std::move(apply_options);
  return futures::While([this, data] {
           if (data->repetitions == data->options.repetitions ||
               (data->options.direction == Direction::kForwards
                    ? undo_stack_.empty()
                    : redo_stack_.empty())) {
             return futures::Past(IterationControlCommand::kStop);
           }

           NonNull<std::shared_ptr<transformation::Stack>> value = [&] {
             switch (data->options.direction) {
               case Direction::kForwards: {
                 auto output = std::move(undo_stack_.back());
                 undo_stack_.pop_back();
                 return output;
               }
               case Direction::kBackwards: {
                 auto output = std::move(redo_stack_.back().redo);
                 redo_stack_.pop_back();
                 return output;
               }
             }
             LOG(FATAL) << "Invalid direction value.";
           }();

           return data->options.callback(value.value())
               .Transform([this, data, value](transformation::Result result) {
                 if (result.modified_buffer ||
                     data->options.mode == ApplyOptions::Mode::kOnlyOne) {
                   data->repetitions++;
                 }
                 switch (data->options.redo_mode) {
                   case ApplyOptions::RedoMode::kIgnore:
                     break;
                   case ApplyOptions::RedoMode::kPopulate:
                     switch (data->options.direction) {
                       case Direction::kForwards:
                         redo_stack_.push_back(RedoStackEntry{
                             .undo = value,
                             .redo = std::move(result.undo_stack)});
                         break;
                       case Direction::kBackwards:
                         undo_stack_.push_back(std::move(result.undo_stack));
                         break;
                     }
                     break;
                 }
                 return IterationControlCommand::kContinue;
               });
           ;
         })
      .Transform([](IterationControlCommand) { return EmptyValue(); });
}

}  // namespace afc::editor
