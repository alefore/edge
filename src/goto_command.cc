#include "src/goto_command.h"

#include <glog/logging.h>

#include <cmath>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/language/lazy_string/functional.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/set_position.h"

namespace afc::editor {
using infrastructure::Tracker;
using language::MakeNonNullUnique;
using language::NonNull;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;

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
                       Direction direction, size_t repetitions, size_t calls) {
  CHECK_LE(prefix_len, suffix_start);
  CHECK_LE(suffix_start, elements);
  if (calls > 1) {
    return ComputePosition(prefix_len, suffix_start, elements,
                           ReverseDirection(direction), repetitions, calls - 2);
  }
  if (calls == 1) {
    return ComputePosition(0, elements, elements, direction, repetitions, 0);
  }

  switch (direction) {
    case Direction::kForwards:
      return std::min(prefix_len + repetitions - 1, elements);
    case Direction::kBackwards:
      return suffix_start - std::min(suffix_start, repetitions - 1);
  }
  LOG(FATAL) << "Invalid direction.";
  return 0;
}

std::optional<LineColumn> ComputeGoToPosition(Structure structure,
                                              const OpenBuffer& buffer,
                                              const Modifiers& modifiers,
                                              LineColumn position, int calls) {
  if (structure == Structure::kChar) {
    const std::wstring& line_prefix_characters =
        buffer.Read(buffer_variables::line_prefix_characters);
    const auto& line = buffer.LineAt(position.line);
    if (line == nullptr) return std::nullopt;
    ColumnNumber start =
        FindFirstColumnWithPredicate(line->contents().value(), [&](ColumnNumber,
                                                                   wchar_t c) {
          return line_prefix_characters.find(c) == std::wstring::npos;
        }).value_or(line->EndColumn());
    ColumnNumber end = line->EndColumn();
    while (start + ColumnNumberDelta(1) < end &&
           (line_prefix_characters.find(
                line->get(end - ColumnNumberDelta(1))) != std::wstring::npos)) {
      end--;
    }
    position.column = ColumnNumber(ComputePosition(
        start.read(), end.read(), line->EndColumn().read(), modifiers.direction,
        modifiers.repetitions.value_or(1), calls));
    CHECK_LE(position.column, line->EndColumn());
    return position;
  } else if (structure == Structure::kSymbol) {
    position.column = modifiers.direction == Direction::kBackwards
                          ? buffer.LineAt(position.line)->EndColumn()
                          : ColumnNumber();

    VLOG(4) << "Start SYMBOL GotoCommand: " << modifiers;
    Range range = buffer.FindPartialRange(modifiers, position);
    switch (modifiers.direction) {
      case Direction::kForwards: {
        Modifiers modifiers_copy = modifiers;
        modifiers_copy.repetitions = 1;
        range = buffer.FindPartialRange(
            modifiers_copy, buffer.contents().PositionBefore(range.end));
        position = range.begin;
      } break;

      case Direction::kBackwards: {
        Modifiers modifiers_copy = modifiers;
        modifiers_copy.repetitions = 1;
        modifiers_copy.direction = Direction::kForwards;
        range = buffer.FindPartialRange(modifiers_copy, range.begin);
        position = buffer.contents().PositionBefore(range.end);
      } break;
    }
    return position;
  } else if (structure == Structure::kLine) {
    size_t lines = buffer.EndLine().read();
    position.line =
        LineNumber(ComputePosition(0, lines, lines, modifiers.direction,
                                   modifiers.repetitions.value_or(1), calls));
    CHECK_LE(position.line, LineNumber(0) + buffer.contents().size());
    return position;
  } else if (structure == Structure::kMark) {
    // Navigates marks in the current buffer.
    const std::multimap<LineColumn, LineMarks::Mark>& marks =
        buffer.GetLineMarks();
    std::vector<std::pair<LineColumn, LineMarks::Mark>> lines;
    std::unique_copy(marks.begin(), marks.end(), std::back_inserter(lines),
                     [](const std::pair<LineColumn, LineMarks::Mark>& entry1,
                        const std::pair<LineColumn, LineMarks::Mark>& entry2) {
                       return (entry1.first.line == entry2.first.line);
                     });
    size_t index =
        ComputePosition(0, lines.size(), lines.size(), modifiers.direction,
                        modifiers.repetitions.value_or(1), calls);
    CHECK_LE(index, lines.size());
    return lines.at(index).first;
  } else if (structure == Structure::kPage) {
    CHECK_GT(buffer.contents().size(), LineNumberDelta(0));
    std::optional<LineColumnDelta> view_size =
        buffer.display_data().view_size().Get();
    auto lines = view_size.has_value() ? view_size->line : LineNumberDelta(1);
    size_t pages = ceil(static_cast<double>(buffer.contents().size().read()) /
                        lines.read());
    position.line =
        LineNumber(0) +
        lines * ComputePosition(0, pages, pages, modifiers.direction,
                                modifiers.repetitions.value_or(1), calls);
    CHECK_LT(position.line.ToDelta(), buffer.contents().size());
    return position;
  } else
    return std::nullopt;
}

class GotoCommand : public Command {
 public:
  GotoCommand(EditorState& editor_state, size_t calls)
      : editor_state_(editor_state), calls_(calls % 4) {}

  std::wstring Description() const override {
    return L"goes to Rth structure from the beginning";
  }
  std::wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t c) override {
    if (c != 'g') {
      auto old_mode = editor_state_.set_keyboard_redirect(nullptr);
      editor_state_.ProcessInput(c);
      return;
    }
    auto structure = editor_state_.structure();
    if (structure == Structure::kChar || structure == Structure::kSymbol ||
        structure == Structure::kLine || structure == Structure::kMark ||
        structure == Structure::kPage || structure == Structure::kSearch ||
        structure == Structure::kCursor) {
      editor_state_.ApplyToActiveBuffers(
          MakeNonNullUnique<GotoTransformation>(calls_));
    } else if (structure == Structure::kBuffer) {
      size_t buffers = editor_state_.buffers()->size();
      size_t position =
          ComputePosition(0, buffers, buffers, editor_state_.direction(),
                          editor_state_.repetitions().value_or(1), calls_);
      CHECK_LT(position, editor_state_.buffers()->size());
      auto it = editor_state_.buffers()->begin();
      advance(it, position);
      if (auto current = editor_state_.current_buffer();
          !current.has_value() ||
          &it->second.ptr().value() != &current->ptr().value()) {
        editor_state_.set_current_buffer(it->second,
                                         CommandArgumentModeApplyMode::kFinal);
      }
    }

    editor_state_.PushCurrentPosition();
    editor_state_.ResetStructure();
    editor_state_.ResetDirection();
    editor_state_.ResetRepetitions();
    editor_state_.set_keyboard_redirect(
        std::make_unique<GotoCommand>(editor_state_, calls_ + 1));
  }

 private:
  EditorState& editor_state_;
  const size_t calls_;
};

}  // namespace

GotoTransformation::GotoTransformation(int calls) : calls_(calls) {}

std::wstring GotoTransformation::Serialize() const {
  return L"GotoTransformation()";
}

futures::Value<CompositeTransformation::Output> GotoTransformation::Apply(
    CompositeTransformation::Input input) const {
  static Tracker tracker(L"GotoTransformation::Apply");
  auto call = tracker.Call();
  auto position = ComputeGoToPosition(input.modifiers.structure, input.buffer,
                                      input.modifiers, input.position, calls_);
  return futures::Past(
      position.has_value() ? Output::SetPosition(position.value()) : Output());
}

NonNull<std::unique_ptr<Command>> NewGotoCommand(EditorState& editor_state) {
  return MakeNonNullUnique<GotoCommand>(editor_state, 0);
}

}  // namespace afc::editor
