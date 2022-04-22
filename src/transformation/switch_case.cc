#include "src/transformation/switch_case.h"

#include "src/buffer.h"
#include "src/buffer_name.h"
#include "src/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm_transformation.h"

namespace afc::editor {
using language::NonNull;
std::wstring SwitchCaseTransformation::Serialize() const {
  return L"SwitchCaseTransformation();";
}

futures::Value<CompositeTransformation::Output> SwitchCaseTransformation::Apply(
    Input input) const {
  auto contents_to_insert = std::make_unique<BufferContents>();
  VLOG(5) << "Switch Case Transformation at " << input.position << ": "
          << input.modifiers << ": Range: " << input.range;
  LineColumn i = input.range.begin;
  while (i < input.range.end) {
    auto line = input.buffer->LineAt(i.line);
    if (line == nullptr) {
      break;
    }
    if (i.column >= line->EndColumn()) {  // Switch to the next line.
      contents_to_insert->push_back(NonNull<std::shared_ptr<Line>>());
      i = LineColumn(i.line + LineNumberDelta(1));
    } else {
      wchar_t c = line->get(i.column);
      contents_to_insert->AppendToLine(
          contents_to_insert->EndLine(),
          Line(wstring(1, iswupper(c) ? towlower(c) : towupper(c))));
      i.column++;
    }
  }

  Output output = Output::SetPosition(input.range.begin);

  output.Push(transformation::Delete{
      .modifiers = {.repetitions = contents_to_insert->CountCharacters(),
                    .paste_buffer_behavior =
                        Modifiers::PasteBufferBehavior::kDoNothing},
      .mode = transformation::Input::Mode::kFinal});

  output.Push(transformation::Insert{
      .contents_to_insert = std::move(contents_to_insert),
      .final_position = input.modifiers.direction == Direction::kBackwards
                            ? transformation::Insert::FinalPosition::kStart
                            : transformation::Insert::FinalPosition::kEnd,
      .modifiers_set =
          input.mode == transformation::Input::Mode::kPreview
              ? LineModifierSet({LineModifier::UNDERLINE, LineModifier::BLUE})
              : std::optional<LineModifierSet>()});

  return futures::Past(std::move(output));
}

std::unique_ptr<CompositeTransformation> SwitchCaseTransformation::Clone()
    const {
  return std::make_unique<SwitchCaseTransformation>();
}

}  // namespace afc::editor
