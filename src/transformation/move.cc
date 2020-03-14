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
namespace transformation {
futures::Value<Transformation::Result> ApplyBase(const SwapActiveCursor&,
                                                 Transformation::Input input) {
  auto active_cursors = input.buffer->active_cursors();
  if (input.position != *active_cursors->active()) {
    LOG(INFO) << "Skipping cursor.";
    return futures::Past(Transformation::Result(input.position));
  }

  Transformation::Result output(input.buffer->FindNextCursor(input.position));
  if (output.position == input.position) {
    LOG(INFO) << "Cursor didn't move.";
    return futures::Past(std::move(output));
  }

  VLOG(5) << "Moving cursor from " << input.position << " to "
          << output.position;

  auto next_it = active_cursors->find(output.position);
  CHECK(next_it != active_cursors->end());
  active_cursors->erase(next_it);
  active_cursors->insert(input.position);
  return futures::Past(std::move(output));
}
};  // namespace transformation

namespace {
class MoveTransformation : public CompositeTransformation {
 public:
  MoveTransformation() {}

  std::wstring Serialize() const override { return L"MoveTransformation()"; }

  futures::Value<Output> Apply(Input input) const override {
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
      position = input.modifiers.direction == Direction::kForwards
                     ? input.range.end
                     : input.range.begin;
    } else if (structure == StructurePage()) {
      static const auto kDefaultScreenLines = LineNumberDelta(24);
      auto view_size = input.buffer->viewers()->view_size();
      auto lines =
          view_size.has_value() ? view_size->line : kDefaultScreenLines;
      Modifiers modifiers;
      modifiers.direction = input.modifiers.direction;
      modifiers.structure = StructureLine();
      modifiers.repetitions =
          (max(0.2, 1.0 - 2.0 * input.buffer->Read(
                                    buffer_variables::margin_lines_ratio)) *
               lines -
           LineNumberDelta(1))
              .line_delta;

      position = MoveLine(input.buffer, input.original_position, modifiers);
    } else if (structure == StructureMark()) {
      position =
          MoveMark(input.buffer, input.original_position, input.modifiers);
    } else if (structure == StructureCursor()) {
      return futures::Past(Output(Build(transformation::SwapActiveCursor())));
    } else {
      input.buffer->status()->SetWarningText(L"Unhandled structure: " +
                                             structure->ToString());
      return futures::Past(Output());
    }
    if (!position.has_value()) {
      return futures::Past(Output());
    }
    if (input.modifiers.repetitions > 1) {
      input.editor->PushPosition(position.value());
    }

    LOG(INFO) << "Move from " << input.original_position << " to "
              << position.value() << " " << input.modifiers;
    return futures::Past(Output::SetPosition(position.value()));
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
        modifiers.direction == Direction::kForwards
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
    int direction = (modifiers.direction == Direction::kBackwards ? -1 : 1);
    size_t repetitions = modifiers.repetitions.value_or(1);
    if (modifiers.direction == Direction::kBackwards &&
        repetitions > position.line.line) {
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
      case Direction::kForwards:
        return GetMarkPosition(marks->begin(), marks->end(), position,
                               modifiers);
        break;
      case Direction::kBackwards:
        return GetMarkPosition(marks->rbegin(), marks->rend(), position,
                               modifiers);
    }
    CHECK(false);
    return std::nullopt;
  }
};
}  // namespace

std::unique_ptr<CompositeTransformation> NewMoveTransformation() {
  return std::make_unique<MoveTransformation>();
}
}  // namespace afc::editor
