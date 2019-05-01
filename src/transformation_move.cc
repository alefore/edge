#include "src/transformation_move.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/line_marks.h"
#include "src/transformation.h"

namespace afc {
namespace editor {

namespace {

class MoveTransformation : public Transformation {
 public:
  MoveTransformation(const Modifiers& modifiers) : modifiers_(modifiers) {}

  void Apply(OpenBuffer* buffer, Result* result) const override {
    VLOG(1) << "Move Transformation starts: "
            << buffer->Read(buffer_variables::name) << " " << modifiers_;
    CHECK(buffer != nullptr);
    CHECK(result != nullptr);
    auto editor_state = buffer->editor();
    auto root = buffer->parse_tree();
    auto current_tree = buffer->current_tree(root.get());
    LineColumn position;
    // TODO: Move to Structure.
    auto structure = modifiers_.structure;
    if (structure == StructureLine()) {
      position = MoveLine(buffer, result->cursor);
    } else if (structure == StructureChar() || structure == StructureTree() ||
               structure == StructureSymbol() || structure == StructureWord()) {
      position = MoveRange(buffer, result->cursor);
    } else if (structure == StructureMark()) {
      position = MoveMark(buffer, result->cursor);
    } else if (structure == StructureCursor()) {
      // Handles repetitions.
      auto active_cursors = buffer->active_cursors();
      if (result->cursor != *active_cursors->active()) {
        LOG(INFO) << "Skipping cursor.";
        return;
      }

      LineColumn next_cursor = buffer->FindNextCursor(result->cursor);
      if (next_cursor == result->cursor) {
        LOG(INFO) << "Cursor didn't move.";
        return;
      }

      VLOG(5) << "Moving cursor from " << result->cursor << " to "
              << next_cursor;

      auto next_it = active_cursors->find(next_cursor);
      CHECK(next_it != active_cursors->end());
      active_cursors->erase(next_it);
      active_cursors->insert(result->cursor);
      result->cursor = next_cursor;

      editor_state->ScheduleRedraw();
      editor_state->ResetRepetitions();
      editor_state->ResetStructure();
      editor_state->ResetDirection();
      return;
    } else {
      LOG(FATAL) << "Unhandled structure: " << structure->ToString();
    }
    LOG(INFO) << "Move from " << result->cursor << " to " << position << " "
              << modifiers_;
    NewGotoPositionTransformation(position)->Apply(buffer, result);
    if (modifiers_.repetitions > 1) {
      editor_state->PushPosition(result->cursor);
    }
    if (buffer->active_cursors()->size() > 1 ||
        buffer->current_tree(root.get()) != current_tree) {
      editor_state->ScheduleRedraw();
    }
    editor_state->ResetRepetitions();
    editor_state->ResetStructure();
    editor_state->ResetDirection();
  }

  unique_ptr<Transformation> Clone() const override {
    return NewMoveTransformation(modifiers_);
  }

 private:
  static bool StringContains(const wstring& str, int c) {
    return str.find(static_cast<char>(c)) != wstring::npos;
  }

  template <typename Iterator>
  static LineColumn GetMarkPosition(Iterator it_begin, Iterator it_end,
                                    LineColumn current,
                                    const Modifiers& modifiers) {
    using P = pair<const size_t, LineMarks::Mark>;
    Iterator it = std::upper_bound(
        it_begin, it_end, P(current.line.line, LineMarks::Mark()),
        modifiers.direction == FORWARDS
            ? [](const P& a, const P& b) { return a.first < b.first; }
            : [](const P& a, const P& b) { return a.first > b.first; });
    if (it == it_end) {
      return current;
    }

    for (size_t i = 1; i < modifiers.repetitions; i++) {
      size_t position = it->first;
      ++it;
      // Skip more marks for the same line.
      while (it != it_end && it->first == position) {
        ++it;
      }
      if (it == it_end) {
        // Can't move past the current mark.
        return LineColumn(LineNumber(position));
      }
    }

    return it->second.target;
  }

  LineColumn MoveLine(OpenBuffer* buffer, LineColumn position) const {
    int direction = (modifiers_.direction == BACKWARDS ? -1 : 1);
    size_t repetitions = modifiers_.repetitions;
    if (modifiers_.direction == BACKWARDS && repetitions > position.line.line) {
      position.line = LineNumber(0);
    } else {
      position.line += LineNumberDelta(direction * repetitions);
      position.line = min(position.line, buffer->contents()->EndLine());
    }
    return position;
  }

  LineColumn MoveRange(OpenBuffer* buffer, LineColumn position) const {
    Range range = buffer->FindPartialRange(modifiers_, position);
    CHECK_LE(range.begin, range.end);
    return modifiers_.direction == FORWARDS ? range.end : range.begin;
  }

  LineColumn MoveMark(OpenBuffer* buffer, LineColumn position) const {
    const multimap<size_t, LineMarks::Mark>* marks = buffer->GetLineMarks();

    switch (modifiers_.direction) {
      case FORWARDS:
        return GetMarkPosition(marks->begin(), marks->end(), position,
                               modifiers_);
        break;
      case BACKWARDS:
        return GetMarkPosition(marks->rbegin(), marks->rend(), position,
                               modifiers_);
    }
    CHECK(false);
    return LineColumn();
  }

  const Modifiers modifiers_;
};

}  // namespace

std::unique_ptr<Transformation> NewMoveTransformation(
    const Modifiers& modifiers) {
  return std::make_unique<MoveTransformation>(modifiers);
}

}  // namespace editor
}  // namespace afc
