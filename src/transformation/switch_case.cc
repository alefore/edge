#include "src/transformation/switch_case.h"

#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class SwitchCaseTransformation : public CompositeTransformation {
 public:
  std::wstring Serialize() const override {
    return L"SwitchCaseTransformation();";
  }

  futures::Value<Output> Apply(Input input) const override {
    auto buffer_to_insert =
        OpenBuffer::New({.editor = input.editor, .name = L"- text inserted"});
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

    transformation::Delete delete_options;
    delete_options.modifiers.paste_buffer_behavior =
        Modifiers::PasteBufferBehavior::kDoNothing;
    delete_options.modifiers.repetitions =
        buffer_to_insert->contents()->CountCharacters();
    delete_options.mode = Transformation::Input::Mode::kFinal;
    output.Push(std::move(delete_options));

    transformation::Insert insert_options(buffer_to_insert);
    if (input.modifiers.direction == Direction::kBackwards) {
      insert_options.final_position =
          transformation::Insert::FinalPosition::kStart;
    }
    if (input.mode == Transformation::Input::Mode::kPreview) {
      insert_options.modifiers_set = {LineModifier::UNDERLINE,
                                      LineModifier::BLUE};
    }
    output.Push(std::move(insert_options));
    if (input.mode == Transformation::Input::Mode::kPreview) {
      output.Push(
          transformation::Build(transformation::SetPosition(input.position)));
    }
    return futures::Past(std::move(output));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<SwitchCaseTransformation>();
  }
};
}  // namespace

std::unique_ptr<Transformation> NewSwitchCaseTransformation(
    Modifiers modifiers) {
  return NewTransformation(std::move(modifiers),
                           std::make_unique<SwitchCaseTransformation>());
}
}  // namespace afc::editor
