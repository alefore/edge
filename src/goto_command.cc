#include "src/goto_command.h"

#include <glog/logging.h>

#include <cmath>

#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/functional.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/set_position.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::infrastructure::ExtendedChar;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineColumnDelta;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::Range;

namespace afc::editor {

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
    const std::unordered_set<wchar_t> line_prefix_characters =
        container::MaterializeUnorderedSet(
            buffer.Read(buffer_variables::line_prefix_characters));
    const auto& line = buffer.LineAt(position.line);
    if (!line.has_value()) return std::nullopt;
    ColumnNumber start =
        FindFirstNotOf(line->contents(), line_prefix_characters)
            .value_or(line->EndColumn());
    ColumnNumber end = line->EndColumn();
    while (start + ColumnNumberDelta(1) < end &&
           (line_prefix_characters.contains(
               line->get(end - ColumnNumberDelta(1)))))
      end--;
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
            modifiers_copy,
            buffer.contents().snapshot().PositionBefore(range.end()));
        position = range.begin();
      } break;

      case Direction::kBackwards: {
        Modifiers modifiers_copy = modifiers;
        modifiers_copy.repetitions = 1;
        modifiers_copy.direction = Direction::kForwards;
        range = buffer.FindPartialRange(modifiers_copy, range.begin());
        position = buffer.contents().snapshot().PositionBefore(range.end());
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

  LazyString Description() const override {
    return LazyString{L"goes to Rth structure from the beginning"};
  }
  CommandCategory Category() const override {
    return CommandCategory::kNavigate();
  }

  void ProcessInput(ExtendedChar c) override {
    if (c != ExtendedChar('g')) {
      // The call to set_keyboard_redirect may delete `this`, so we ensure we
      // capture explicitly everything we need:
      std::invoke([&editor = editor_state_, c] {
        editor.set_keyboard_redirect(std::nullopt);
        editor.ProcessInput({c});
      });
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
      std::vector<gc::Root<OpenBuffer>> buffers =
          editor_state_.buffer_registry().buffers();
      size_t position = ComputePosition(
          0, buffers.size(), buffers.size(), editor_state_.direction(),
          editor_state_.repetitions().value_or(1), calls_);
      CHECK_LT(position, buffers.size());
      auto it = buffers.begin();
      advance(it, position);
      if (auto current = editor_state_.current_buffer();
          !current.has_value() ||
          &it->ptr().value() != &current->ptr().value()) {
        editor_state_.set_current_buffer(*it,
                                         CommandArgumentModeApplyMode::kFinal);
      }
    }

    editor_state_.ResetStructure();
    editor_state_.ResetDirection();
    editor_state_.ResetRepetitions();
    editor_state_.set_keyboard_redirect(
        editor_state_.gc_pool().NewRoot<InputReceiver>(
            MakeNonNullUnique<GotoCommand>(editor_state_, calls_ + 1)));
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
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
  TRACK_OPERATION(GotoTransformation_Apply);
  auto position = ComputeGoToPosition(input.modifiers.structure, input.buffer,
                                      input.modifiers, input.position, calls_);
  return futures::Past(
      position.has_value() ? Output::SetPosition(position.value()) : Output());
}

gc::Root<Command> NewGotoCommand(EditorState& editor_state) {
  return editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<GotoCommand>(editor_state, 0));
}

}  // namespace afc::editor
