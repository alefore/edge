#include "src/transformation/switch_case.h"

#include "src/buffer.h"
#include "src/buffer_name.h"
#include "src/char_buffer.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm_transformation.h"

namespace afc::editor {
std::wstring SwitchCaseTransformation::Serialize() const {
  return L"SwitchCaseTransformation();";
}

futures::Value<CompositeTransformation::Output> SwitchCaseTransformation::Apply(
    Input input) const {
  auto buffer_to_insert = OpenBuffer::New(
      {.editor = input.editor, .name = BufferName(L"- text inserted")});
  VLOG(5) << "Switch Case Transformation at " << input.position << ": "
          << input.modifiers << ": Range: " << input.range;
  LineColumn i = input.range.begin;
  while (i < input.range.end) {
    auto line = input.buffer->LineAt(i.line);
    if (line == nullptr) {
      break;
    }
    if (i.column >= line->EndColumn()) {  // Switch to the next line.
      buffer_to_insert->AppendEmptyLine();
      i = LineColumn(i.line + LineNumberDelta(1));
    } else {
      wchar_t c = line->get(i.column);
      buffer_to_insert->AppendToLastLine(
          NewLazyString(wstring(1, iswupper(c) ? towlower(c) : towupper(c))));
      i.column++;
    }
  }

  Output output = Output::SetPosition(input.range.begin);

  output.Push(transformation::Delete{
      .modifiers = {.repetitions =
                        buffer_to_insert->contents()->CountCharacters(),
                    .delete_behavior = Modifiers::DeleteBehavior::kDeleteText,
                    .paste_buffer_behavior =
                        Modifiers::PasteBufferBehavior::kDoNothing},
      .mode = transformation::Input::Mode::kFinal});

  transformation::Insert insert_options{.buffer_to_insert =
                                            std::move(buffer_to_insert)};
  if (input.modifiers.direction == Direction::kBackwards) {
    insert_options.final_position =
        transformation::Insert::FinalPosition::kStart;
  }
  if (input.mode == transformation::Input::Mode::kPreview) {
    insert_options.modifiers_set = {LineModifier::UNDERLINE,
                                    LineModifier::BLUE};
  }
  output.Push(std::move(insert_options));
  return futures::Past(std::move(output));
}

std::unique_ptr<CompositeTransformation> SwitchCaseTransformation::Clone()
    const {
  return std::make_unique<SwitchCaseTransformation>();
}

}  // namespace afc::editor
