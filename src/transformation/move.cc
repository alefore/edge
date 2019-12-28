#include "src/transformation/move.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/line_marks.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/set_position.h"

namespace afc::editor {
namespace {
class MoveCursorTransformation : public Transformation {
  void Apply(const Input&, Result* result) const override {
    // Handles repetitions.
    auto active_cursors = result->buffer->active_cursors();
    if (result->cursor != *active_cursors->active()) {
      LOG(INFO) << "Skipping cursor.";
      return;
    }

    LineColumn next_cursor = result->buffer->FindNextCursor(result->cursor);
    if (next_cursor == result->cursor) {
      LOG(INFO) << "Cursor didn't move.";
      return;
    }

    VLOG(5) << "Moving cursor from " << result->cursor << " to " << next_cursor;

    auto next_it = active_cursors->find(next_cursor);
    CHECK(next_it != active_cursors->end());
    active_cursors->erase(next_it);
    active_cursors->insert(result->cursor);
    result->cursor = next_cursor;
  }
  std::unique_ptr<Transformation> Clone() const override {
    return std::unique_ptr<MoveCursorTransformation>();
  }
};

class MoveTransformation : public CompositeTransformation {
 public:
  MoveTransformation() {}

  std::wstring Serialize() const override { return L"MoveTransformation()"; }

  void Apply(Input input) const override {
    CHECK(input.buffer != nullptr);
    VLOG(1) << "Move Transformation starts: "
            << input.buffer->Read(buffer_variables::name) << " "
            << input.modifiers;
    std::optional<LineColumn> position;
    // TODO: Move to Structure.
    auto structure = input.modifiers.structure;
    if (structure == StructureLine()) {
      position =
          MoveLine(input.buffer, input.original_position, input.modifiers);
    } else if (structure == StructureChar() || structure == StructureTree() ||
               structure == StructureSymbol() || structure == StructureWord()) {
      CHECK_LE(input.range.begin, input.range.end);
      position = input.modifiers.direction == FORWARDS ? input.range.end
                                                       : input.range.begin;
    } else if (structure == StructureMark()) {
      position =
          MoveMark(input.buffer, input.original_position, input.modifiers);
    } else if (structure == StructureCursor()) {
      input.push(std::make_unique<MoveCursorTransformation>());

      return;
    } else {
      input.buffer->status()->SetWarningText(L"Unhandled structure: " +
                                             structure->ToString());
      return;
    }
    if (position.has_value()) {
      LOG(INFO) << "Move from " << input.original_position << " to "
                << position.value() << " " << input.modifiers;
      input.push(NewSetPositionTransformation(position.value()));
      if (input.modifiers.repetitions > 1) {
        input.editor->PushPosition(position.value());
      }
    }
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<MoveTransformation>();
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

  LineColumn MoveLine(const OpenBuffer* buffer, LineColumn position,
                      const Modifiers& modifiers) const {
    int direction = (modifiers.direction == BACKWARDS ? -1 : 1);
    size_t repetitions = modifiers.repetitions;
    if (modifiers.direction == BACKWARDS && repetitions > position.line.line) {
      position.line = LineNumber(0);
    } else {
      position.line += LineNumberDelta(direction * repetitions);
      position.line = min(position.line, buffer->contents()->EndLine());
    }
    return position;
  }

  std::optional<LineColumn> MoveMark(const OpenBuffer* buffer,
                                     LineColumn position,
                                     const Modifiers& modifiers) const {
    const multimap<size_t, LineMarks::Mark>* marks = buffer->GetLineMarks();

    switch (modifiers.direction) {
      case FORWARDS:
        return GetMarkPosition(marks->begin(), marks->end(), position,
                               modifiers);
        break;
      case BACKWARDS:
        return GetMarkPosition(marks->rbegin(), marks->rend(), position,
                               modifiers);
    }
    CHECK(false);
    return std::nullopt;
  }
};
}  // namespace

std::unique_ptr<Transformation> NewMoveTransformation(
    const Modifiers& modifiers) {
  return NewTransformation(modifiers, std::make_unique<MoveTransformation>());
}
}  // namespace afc::editor
