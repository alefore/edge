#include "goto_command.h"

#include <cmath>

#include <glog/logging.h>

#include "buffer.h"
#include "buffer_variables.h"
#include "command.h"
#include "editor.h"
#include "transformation.h"

namespace afc {
namespace editor {

namespace {
// Arguments:
//   prefix_len: The size of the prefix that we skip when calls is 0.
//   suffix_start: The position where the suffix starts. This is the base when
//       calls is 2.
//   elements: The total number of elements.
//   direction: The direction of movement.
//   repetitions: The nth element to jump to.
//   structure_range: The StructureRange. If FROM_CURRENT_POSITION_TO_END, it
//       reverses the direction.
//   calls: The number of consecutive number of times this command has run.
size_t ComputePosition(size_t prefix_len, size_t suffix_start, size_t elements,
                       Direction direction, size_t repetitions,
                       Modifiers::StructureRange structure_range,
                       size_t calls) {
  CHECK_LE(prefix_len, suffix_start);
  CHECK_LE(suffix_start, elements);
  if (calls > 1) {
    return ComputePosition(prefix_len, suffix_start, elements,
                           ReverseDirection(direction), repetitions,
                           structure_range, calls - 2);
  }
  if (calls == 1) {
    return ComputePosition(0, elements, elements, direction, repetitions,
                           structure_range, 0);
  }
  if (structure_range == Modifiers::FROM_CURRENT_POSITION_TO_END) {
    return ComputePosition(prefix_len, suffix_start, elements,
                           ReverseDirection(direction), repetitions,
                           Modifiers::ENTIRE_STRUCTURE, calls);
  }

  if (direction == FORWARDS) {
    return min(prefix_len + repetitions - 1, elements);
  } else {
    return suffix_start - min(suffix_start, repetitions - 1);
  }
}

class GotoCharTransformation : public Transformation {
 public:
  GotoCharTransformation(int calls) : calls_(calls) {}

  void Apply(EditorState* editor, OpenBuffer* buffer,
             Result* result) const override {
    const wstring& line_prefix_characters = buffer->read_string_variable(
        buffer_variables::line_prefix_characters());
    const auto& line = buffer->LineAt(result->cursor.line);
    if (line == nullptr) {
      result->success = false;
      return;
    }
    size_t start = 0;
    while (start < line->size() &&
           (line_prefix_characters.find(line->get(start)) != string::npos)) {
      start++;
    }
    size_t end = line->size();
    while (start + 1 < end &&
           (line_prefix_characters.find(line->get(end - 1)) != string::npos)) {
      end--;
    }
    size_t position = ComputePosition(
        start, end, line->size(), editor->direction(), editor->repetitions(),
        editor->structure_range(), calls_);
    CHECK_LE(position, line->size());
    result->made_progress = result->cursor.column != position;
    result->cursor.column = position;
  }

  std::unique_ptr<Transformation> Clone() {
    return std::make_unique<GotoCharTransformation>(calls_);
  }

 private:
  const int calls_;
};

class GotoCommand : public Command {
 public:
  GotoCommand(size_t calls) : calls_(calls % 4) {}

  const wstring Description() {
    return L"goes to Rth structure from the beginning";
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    auto buffer = editor_state->current_buffer()->second;
    if (c != 'g') {
      buffer->ResetMode();
      editor_state->ProcessInput(c);
      return;
    }
    switch (editor_state->structure()) {
      case CHAR:
        buffer->ApplyToCursors(
            std::make_unique<GotoCharTransformation>(calls_));
        break;

      case WORD: {
        LineColumn position(buffer->position().line);
        buffer->AdjustLineColumn(&position);
        if (editor_state->direction() == BACKWARDS) {
          position.column = buffer->LineAt(position.line)->size();
        }

        VLOG(4) << "Start WORD GotoCommand: " << editor_state->modifiers();
        LineColumn start, end;
        if (buffer->FindPartialRange(editor_state->modifiers(), position,
                                     &start, &end)) {
          switch (editor_state->direction()) {
            case FORWARDS: {
              Modifiers modifiers_copy = editor_state->modifiers();
              modifiers_copy.repetitions = 1;
              end = buffer->PositionBefore(end);
              if (buffer->FindPartialRange(modifiers_copy, end, &start, &end)) {
                position = start;
              }
            } break;

            case BACKWARDS: {
              Modifiers modifiers_copy = editor_state->modifiers();
              modifiers_copy.repetitions = 1;
              modifiers_copy.direction = FORWARDS;
              if (buffer->FindPartialRange(modifiers_copy, start, &start,
                                           &end)) {
                position = buffer->PositionBefore(end);
              }
            } break;
          }
          buffer->set_position(position);
        }
      } break;

      case LINE: {
        size_t lines = buffer->contents()->size() - 1;
        size_t position =
            ComputePosition(0, lines, lines, editor_state->direction(),
                            editor_state->repetitions(),
                            editor_state->structure_range(), calls_);
        CHECK_LE(position, buffer->contents()->size());
        buffer->set_current_position_line(position);
      } break;

      case MARK: {
        // Navigates marks in the current buffer.
        const multimap<size_t, LineMarks::Mark>* marks =
            buffer->GetLineMarks(*editor_state);
        vector<pair<size_t, LineMarks::Mark>> lines;
        std::unique_copy(marks->begin(), marks->end(),
                         std::back_inserter(lines),
                         [](const pair<size_t, LineMarks::Mark>& entry1,
                            const pair<size_t, LineMarks::Mark>& entry2) {
                           return (entry1.first == entry2.first);
                         });
        size_t position = ComputePosition(
            0, lines.size(), lines.size(), editor_state->direction(),
            editor_state->repetitions(), editor_state->structure_range(),
            calls_);
        CHECK_LE(position, lines.size());
        buffer->set_current_position_line(lines.at(position).first);
      } break;

      case PAGE: {
        CHECK(!buffer->contents()->empty());
        size_t pages = ceil(static_cast<double>(buffer->contents()->size()) /
                            editor_state->visible_lines());
        size_t position =
            editor_state->visible_lines() *
            ComputePosition(0, pages, pages, editor_state->direction(),
                            editor_state->repetitions(),
                            editor_state->structure_range(), calls_);
        CHECK_LT(position, buffer->contents()->size());
        buffer->set_current_position_line(position);
      } break;

      case SEARCH:
        // TODO: Implement.
        break;

      case CURSOR:
        GotoCursor(editor_state);
        break;

      case BUFFER: {
        size_t buffers = editor_state->buffers()->size();
        size_t position =
            ComputePosition(0, buffers, buffers, editor_state->direction(),
                            editor_state->repetitions(),
                            editor_state->structure_range(), calls_);
        CHECK_LT(position, editor_state->buffers()->size());
        auto it = editor_state->buffers()->begin();
        advance(it, position);
        if (it != editor_state->current_buffer()) {
          editor_state->set_current_buffer(it);
        }
      } break;
    }
    editor_state->PushCurrentPosition();
    editor_state->ScheduleRedraw();
    editor_state->ResetStructure();
    editor_state->ResetDirection();
    editor_state->ResetRepetitions();
    buffer->set_mode(std::make_unique<GotoCommand>(calls_ + 1));
  }

 private:
  void GotoCursor(EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    // TODO: Implement.
#if 0
    auto buffer = editor_state->current_buffer()->second;
    auto cursors = buffer->active_cursors();
    auto modifiers = editor_state->modifiers();
    CursorsSet::iterator current = buffer->current_cursor();
    for (size_t i = 0; i < modifiers.repetitions && current != cursors->begin();
         i++) {
      --current;
    }
#endif
  }

  const size_t calls_;
};

}  // namespace

std::unique_ptr<Command> NewGotoCommand() {
  return std::make_unique<GotoCommand>(0);
}

}  // namespace editor
}  // namespace afc
