#include "src/transformation/switch_case.h"

#include "src/buffer.h"
#include "src/buffer_name.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/transformation/vm.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;
using language::text::LineColumn;
using language::text::LineNumberDelta;

std::wstring SwitchCaseTransformation::Serialize() const {
  return L"SwitchCaseTransformation();";
}

futures::Value<CompositeTransformation::Output> SwitchCaseTransformation::Apply(
    Input input) const {
  NonNull<std::unique_ptr<BufferContents>> contents_to_insert;
  VLOG(5) << "Switch Case Transformation at " << input.position << ": "
          << input.modifiers << ": Range: " << input.range;
  LineColumn i = input.range.begin;
  while (i < input.range.end) {
    NonNull<std::shared_ptr<const Line>> line =
        input.buffer.contents().at(i.line);
    if (i.column >= line->EndColumn()) {  // Switch to the next line.
      contents_to_insert->push_back(NonNull<std::shared_ptr<Line>>());
      i = LineColumn(i.line + LineNumberDelta(1));
    } else {
      wchar_t c = line->get(i.column);
      contents_to_insert->AppendToLine(
          contents_to_insert->EndLine(),
          Line(std::wstring(1, iswupper(c) ? towlower(c) : towupper(c))));
      i.column++;
    }
  }

  Output output = Output::SetPosition(input.range.begin);

  output.Push(transformation::Delete{
      .modifiers = {.repetitions = contents_to_insert->CountCharacters(),
                    .paste_buffer_behavior =
                        Modifiers::PasteBufferBehavior::kDoNothing},
      .mode = transformation::Input::Mode::kFinal,
      .initiator = transformation::Delete::Initiator::kInternal});

  output.Push(transformation::Insert{
      .contents_to_insert = std::move(contents_to_insert),
      .final_position = input.modifiers.direction == Direction::kBackwards
                            ? transformation::Insert::FinalPosition::kStart
                            : transformation::Insert::FinalPosition::kEnd,
      .modifiers_set =
          input.mode == transformation::Input::Mode::kPreview
              ? LineModifierSet({LineModifier::kUnderline, LineModifier::kBlue})
              : std::optional<LineModifierSet>()});

  return futures::Past(std::move(output));
}
}  // namespace afc::editor
