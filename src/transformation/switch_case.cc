#include "src/transformation/switch_case.h"

#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class SwitchCaseTransformation : public CompositeTransformation {
 public:
  std::wstring Serialize() const override {
    return L"SwitchCaseTransformation();";
  }

  void Apply(Input input) const override {
    input.push(NewSetPositionTransformation(input.range.begin));
    auto buffer_to_insert =
        std::make_shared<OpenBuffer>(input.editor, L"- text inserted");
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

    DeleteOptions delete_options;
    delete_options.copy_to_paste_buffer = false;
    delete_options.modifiers.repetitions =
        buffer_to_insert->contents()->CountCharacters();
    delete_options.mode = Transformation::Result::Mode::kFinal;
    input.push(NewDeleteTransformation(std::move(delete_options)));

    InsertOptions insert_options;
    insert_options.buffer_to_insert = buffer_to_insert;
    if (input.modifiers.direction == BACKWARDS) {
      insert_options.final_position = InsertOptions::FinalPosition::kStart;
    }
    if (input.mode == Transformation::Result::Mode::kPreview) {
      insert_options.modifiers_set = {LineModifier::UNDERLINE,
                                      LineModifier::BLUE};
    }
    input.push(NewInsertBufferTransformation(std::move(insert_options)));
    if (input.mode == Transformation::Result::Mode::kPreview) {
      input.push(NewSetPositionTransformation(input.position));
    }
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
