#include "transformation_move.h"

#include <algorithm>

#include <glog/logging.h>

#include "buffer.h"
#include "direction.h"
#include "editor.h"
#include "line_marks.h"
#include "transformation.h"

namespace afc {
namespace editor {

namespace {

class MoveTransformation : public Transformation {
 public:
  MoveTransformation(const Modifiers& modifiers) : modifiers_(modifiers) {}

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    CHECK(result);
    auto current_tree = buffer->current_tree();
    LineColumn position;
    switch (modifiers_.structure) {
      case CHAR:
        buffer->CheckPosition();
        buffer->MaybeAdjustPositionCol();
        if (buffer->current_line() == nullptr) { return; }
        position = MoveCharacter(buffer);
        break;
      case LINE:
        position = MoveLine(buffer);
        break;
      case TREE:
      case WORD:
        position = MoveRange(editor_state, buffer);
        break;
      case MARK:
        position = MoveMark(editor_state, buffer);
        break;

      case CURSOR:
        // Handles repetitions.
        buffer->VisitNextCursor();
        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
        editor_state->ResetDirection();
        return;
      default:
        CHECK(false);
    }
    LOG(INFO) << "Move to: " << position;
    NewGotoPositionTransformation(position)
        ->Apply(editor_state, buffer, result);
    if (modifiers_.repetitions > 1) {
      editor_state->PushCurrentPosition();
    }
    if (buffer->active_cursors()->size() > 1
        || buffer->current_tree() != current_tree) {
      editor_state->ScheduleRedraw();
    }
    editor_state->ResetRepetitions();
    editor_state->ResetStructure();
    editor_state->ResetDirection();
  }

  unique_ptr<Transformation> Clone() {
    return NewMoveTransformation(modifiers_);
  }

 private:
  LineColumn MoveCharacter(OpenBuffer* buffer)
      const {
    LineColumn position = buffer->position();
    switch (modifiers_.direction) {
      case FORWARDS:
        CHECK(buffer->current_line() != nullptr);
        position.column = min(position.column + modifiers_.repetitions,
            buffer->current_line()->size());
        break;
      case BACKWARDS:
        position.column -= min(position.column, modifiers_.repetitions);
        break;
    }
    return position;
  }

  static bool StringContains(const wstring& str, int c) {
    return str.find(static_cast<char>(c)) != wstring::npos;
  }

  template <typename Iterator>
  static LineColumn GetMarkPosition(
      Iterator it_begin, Iterator it_end, LineColumn current,
      const Modifiers& modifiers) {
    using P = pair<const size_t, LineMarks::Mark>;
    Iterator it = std::upper_bound(
        it_begin, it_end, P(current.line, LineMarks::Mark()),
        modifiers.direction == FORWARDS
            ? [](const P& a, const P& b) { return a.first < b.first; }
            : [](const P& a, const P& b) { return a.first > b.first; });
    if (it == it_end) {
      return current;
    }

    for (size_t i = 1; i < modifiers.repetitions; i ++) {
      size_t position = it->first;
      ++it;
      // Skip more marks for the same line.
      while (it != it_end && it->first == position) {
        ++it;
      }
      if (it == it_end) {
        // Can't move past the current mark.
        return position;
      }
    }

    return it->second.target;
  }

  LineColumn MoveLine(OpenBuffer* buffer) const {
    int direction = (modifiers_.direction == BACKWARDS ? -1 : 1);
    size_t current = buffer->current_position_line();
    int repetitions = min(modifiers_.repetitions,
        modifiers_.direction == BACKWARDS
            ? current : buffer->contents()->size() - 1 - current);
    return LineColumn(current + direction * repetitions,
                      buffer->current_position_col());
  }

  LineColumn MoveRange(EditorState*, OpenBuffer* buffer) const {
    LineColumn position = buffer->position();
    LineColumn start, end;

    if (!buffer->FindPartialRange(modifiers_, position, &start, &end)) {
      LOG(INFO) << "Unable to find partial range: " << position;
      return position;
    }

    LOG(INFO) << "Found range: [" << start << ", " << end << ")";

    switch (modifiers_.direction) {
      case FORWARDS:
        {
          Modifiers modifiers_copy = modifiers_;
          modifiers_copy.repetitions = 1;
          if (start > position) {
            end = buffer->PositionBefore(end);
          }
          if (!buffer->FindPartialRange(modifiers_copy, end, &start, &end)) {
            LOG(INFO) << "Unable to find partial range (next): " << end;
            return end;
          }
          LOG(INFO) << "Found range (next): [" << start << ", " << end << ")";
        }
        position = start;
        break;

      case BACKWARDS:
        {
          Modifiers modifiers_copy = modifiers_;
          modifiers_copy.repetitions = 1;
          modifiers_copy.direction = FORWARDS;
          if (end > position) {
            //start = buffer->PositionBefore(start);
          }
          if (!buffer->FindPartialRange(modifiers_copy, start, &start, &end)) {
            LOG(INFO) << "Unable to find partial range (next): " << end;
            return end;
          }
          LOG(INFO) << "Found range (prev): [" << start << ", " << end << ")";
        }
        position = buffer->PositionBefore(end);
        break;
    }

    return position;
  }

  LineColumn MoveMark(EditorState* editor_state, OpenBuffer* buffer) const {
    const multimap<size_t, LineMarks::Mark>* marks =
        buffer->GetLineMarks(*editor_state);

    switch (modifiers_.direction) {
      case FORWARDS:
        return GetMarkPosition(
            marks->begin(), marks->end(), buffer->position(), modifiers_);
        break;
      case BACKWARDS:
        return GetMarkPosition(
            marks->rbegin(), marks->rend(), buffer->position(), modifiers_);
    }
    CHECK(false);
    return LineColumn();
  }

  const Modifiers modifiers_;
};

}  // namespace

unique_ptr<Transformation> NewMoveTransformation(const Modifiers& modifiers) {
  return unique_ptr<Transformation>(new MoveTransformation(modifiers));
}

}  // namespace editor
}  // namespace afc
