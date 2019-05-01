#include "src/goto_command.h"

#include <glog/logging.h>

#include <cmath>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/transformation.h"

namespace afc {
namespace editor {

namespace {
// Arguments:
//   prefix_len: The length of prefix that we skip when calls is 0.
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

  void Apply(OpenBuffer* buffer, Result* result) const override {
    const wstring& line_prefix_characters =
        buffer->Read(buffer_variables::line_prefix_characters);
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
    auto editor = buffer->editor();
    ColumnNumber column = ColumnNumber(ComputePosition(
        start, end, line->size(), editor->direction(), editor->repetitions(),
        editor->structure_range(), calls_));
    CHECK_LE(column, line->EndColumn());
    result->made_progress = result->cursor.column != column;
    result->cursor.column = column;
  }

  std::unique_ptr<Transformation> Clone() const override {
    return std::make_unique<GotoCharTransformation>(calls_);
  }

 private:
  const int calls_;
};

class GotoCommand : public Command {
 public:
  GotoCommand(size_t calls) : calls_(calls % 4) {}

  wstring Description() const override {
    return L"goes to Rth structure from the beginning";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    if (c != 'g') {
      buffer->ResetMode();
      editor_state->ProcessInput(c);
      return;
    }
    // TODO: Move this to Structure.
    auto structure = editor_state->structure();
    if (structure == StructureChar()) {
      buffer->ApplyToCursors(std::make_unique<GotoCharTransformation>(calls_));
    } else if (structure == StructureSymbol()) {
      LineColumn position(buffer->position().line);
      buffer->AdjustLineColumn(&position);
      if (editor_state->direction() == BACKWARDS) {
        position.column = buffer->LineAt(position.line)->EndColumn();
      }

      VLOG(4) << "Start SYMBOL GotoCommand: " << editor_state->modifiers();
      Range range =
          buffer->FindPartialRange(editor_state->modifiers(), position);
      switch (editor_state->direction()) {
        case FORWARDS: {
          Modifiers modifiers_copy = editor_state->modifiers();
          modifiers_copy.repetitions = 1;
          range = buffer->FindPartialRange(modifiers_copy,
                                           buffer->PositionBefore(range.end));
          position = range.begin;
        } break;

        case BACKWARDS: {
          Modifiers modifiers_copy = editor_state->modifiers();
          modifiers_copy.repetitions = 1;
          modifiers_copy.direction = FORWARDS;
          range = buffer->FindPartialRange(modifiers_copy, range.begin);
          position = buffer->PositionBefore(range.end);
        } break;
      }
      buffer->set_position(position);
    } else if (structure == StructureLine()) {
      size_t lines = buffer->EndLine().line;
      LineNumber line =
          LineNumber(ComputePosition(0, lines, lines, editor_state->direction(),
                                     editor_state->repetitions(),
                                     editor_state->structure_range(), calls_));
      CHECK_LE(line, LineNumber(0) + buffer->contents()->size());
      buffer->set_current_position_line(line);
    } else if (structure == StructureMark()) {
      // Navigates marks in the current buffer.
      const multimap<size_t, LineMarks::Mark>* marks = buffer->GetLineMarks();
      vector<pair<size_t, LineMarks::Mark>> lines;
      std::unique_copy(marks->begin(), marks->end(), std::back_inserter(lines),
                       [](const pair<size_t, LineMarks::Mark>& entry1,
                          const pair<size_t, LineMarks::Mark>& entry2) {
                         return (entry1.first == entry2.first);
                       });
      size_t position = ComputePosition(
          0, lines.size(), lines.size(), editor_state->direction(),
          editor_state->repetitions(), editor_state->structure_range(), calls_);
      CHECK_LE(position, lines.size());
      buffer->set_current_position_line(LineNumber(lines.at(position).first));
    } else if (structure == StructurePage()) {
      CHECK_GT(buffer->contents()->size(), LineNumberDelta(0));
      size_t pages = ceil(
          static_cast<double>(buffer->contents()->size().line_delta) /
          editor_state->buffer_tree()->GetActiveLeaf()->lines().line_delta);
      LineNumber position =
          LineNumber(0) +
          editor_state->buffer_tree()->GetActiveLeaf()->lines() *
              ComputePosition(0, pages, pages, editor_state->direction(),
                              editor_state->repetitions(),
                              editor_state->structure_range(), calls_);
      CHECK_LT(position, LineNumber(0) + buffer->contents()->size());
      buffer->set_current_position_line(position);
    } else if (structure == StructureSearch()) {
      // TODO: Implement.
    } else if (structure == StructureCursor()) {
      GotoCursor(editor_state);
    } else if (structure == StructureBuffer()) {
      size_t buffers = editor_state->buffers()->size();
      size_t position = ComputePosition(
          0, buffers, buffers, editor_state->direction(),
          editor_state->repetitions(), editor_state->structure_range(), calls_);
      CHECK_LT(position, editor_state->buffers()->size());
      auto it = editor_state->buffers()->begin();
      advance(it, position);
      if (it->second != editor_state->current_buffer()) {
        editor_state->set_current_buffer(it->second);
      }
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
