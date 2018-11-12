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
    auto root = buffer->parse_tree();
    auto current_tree = buffer->current_tree(root.get());
    LineColumn position;
    switch (modifiers_.structure) {
      case LINE:
        position = MoveLine(buffer, result->cursor);
        break;
      case CHAR:
      case TREE:
      case WORD:
        position = MoveRange(editor_state, buffer, result->cursor);
        break;
      case MARK:
        position = MoveMark(editor_state, buffer, result->cursor);
        break;

      case CURSOR:
        // Handles repetitions.
        {
          auto active_cursors = buffer->active_cursors();
          auto next_cursor = buffer->FindNextCursor(result->cursor);
          if (next_cursor == active_cursors->end()) {
            LOG(INFO) << "Unable to find next cursor.";
            result->success = false;
            return;
          }

          if (*next_cursor == result->cursor) {
            LOG(INFO) << "Cursor didn't move.";
            return;
          }

          LineColumn original_cursor = result->cursor;
          result->cursor = *next_cursor;

          VLOG(5) << "Moving cursor from " << *next_cursor << " to "
                  << original_cursor;

          if (*next_cursor != buffer->position()) {
            active_cursors->erase(next_cursor);
            active_cursors->insert(original_cursor);
          }

          editor_state->ScheduleRedraw();
          editor_state->ResetRepetitions();
          editor_state->ResetStructure();
          editor_state->ResetDirection();
          return;
        }
      default:
        CHECK(false);
    }
    LOG(INFO) << "Move from " << result->cursor << " to " << position << " "
              << modifiers_;
    NewGotoPositionTransformation(position)
        ->Apply(editor_state, buffer, result);
    if (modifiers_.repetitions > 1) {
      editor_state->PushPosition(result->cursor);
    }
    if (buffer->active_cursors()->size() > 1
        || buffer->current_tree(root.get()) != current_tree) {
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

  LineColumn MoveLine(OpenBuffer* buffer, LineColumn position) const {
    int direction = (modifiers_.direction == BACKWARDS ? -1 : 1);
    size_t repetitions = modifiers_.repetitions;
    if (modifiers_.direction == BACKWARDS && repetitions > position.line) {
      position.line = 0;
    } else {
      position.line += direction * repetitions;
      position.line = min(position.line, buffer->contents()->size() - 1);
    }
    return position;
  }

  LineColumn MoveRange(EditorState*, OpenBuffer* buffer, LineColumn position)
      const {
    LineColumn start, end;

    if (!buffer->FindPartialRange(modifiers_, position, &start, &end)) {
      LOG(INFO) << "Unable to find partial range: " << position;
      return position;
    }

    CHECK_LE(start, end);
    return modifiers_.direction == FORWARDS ? end : start;
  }

  LineColumn MoveMark(EditorState* editor_state, OpenBuffer* buffer,
                      LineColumn position) const {
    const multimap<size_t, LineMarks::Mark>* marks =
        buffer->GetLineMarks(*editor_state);

    switch (modifiers_.direction) {
      case FORWARDS:
        return GetMarkPosition(
            marks->begin(), marks->end(), position, modifiers_);
        break;
      case BACKWARDS:
        return GetMarkPosition(
            marks->rbegin(), marks->rend(), position, modifiers_);
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
