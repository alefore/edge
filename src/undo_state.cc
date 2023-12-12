#include "src/undo_state.h"

#include "src/language/container.h"

namespace container = afc::language::container;

using afc::editor::transformation::Variant;
using afc::futures::IterationControlCommand;
using afc::language::EmptyValue;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Success;

namespace afc::editor {
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
    //     v1 -a-> v2 -b-> v3 -c-> v4
    //
    //     undo_stack_: a, b, c
    //     redo_stack_: (empty)
    //
    // If we undo to v2, we are here:
    //
    //     v1 -a-> v2
    //
    //     undo_stack_: a
    //     redo_stack_: c/c', b/b'
    //
    // If change d that transforms v2 to v5 is applied, we want this history:
    //
    //     v1 -a-> v2 -b-> v3 -c-> v4 -c'-> v3 -b'-> v2 -d-> v5
    //       ╰┬───╯  ╰─────┬──────╯  ╰─────┬────────╯  ╰───┬╯
    //        ╰─> history  ╰─> undo chain  ╰─> redo chain  ╰─> last change
    //
    // c' (b') is to the inverse of transformation c (b).
    //
    //     undo_stack_: a, b, c, c', b', d
    //     redo_stack_: (empty)
    std::ranges::copy(redo_stack_ |
                          std::views::transform(&RedoStackEntry::undo) |
                          std::views::reverse,
                      std::back_inserter(undo_stack_));
    std::ranges::copy(
        redo_stack_ | std::views::transform(&RedoStackEntry::redo),
        std::back_inserter(undo_stack_));
    redo_stack_.clear();
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
  auto data = MakeNonNullShared<Data>();
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
