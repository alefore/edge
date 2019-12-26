#include "src/transformation/switch_case.h"

#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
class SwitchCaseTransformation : public Transformation {
 public:
  SwitchCaseTransformation(Modifiers modifiers)
      : modifiers_(std::move(modifiers)) {}

  void Apply(Result* result) const override {
    CHECK(result != nullptr);
    CHECK(result->buffer != nullptr);
    result->buffer->AdjustLineColumn(&result->cursor);
    Range range = result->buffer->FindPartialRange(modifiers_, result->cursor);
    CHECK_LE(range.begin, range.end);
    TransformationStack stack;
    stack.PushBack(NewSetPositionTransformation(range.begin));
    auto buffer_to_insert = std::make_shared<OpenBuffer>(
        result->buffer->editor(), L"- text inserted");
    VLOG(5) << "Switch Case Transformation at " << result->cursor << ": "
            << result->buffer->editor()->modifiers() << ": Range: " << range;
    LineColumn i = range.begin;
    DeleteOptions options;
    options.copy_to_paste_buffer = false;
    options.modifiers.repetitions = 0;
    while (i < range.end) {
      auto line = result->buffer->LineAt(i.line);
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
      options.modifiers.repetitions++;
    }
    stack.PushBack(std::make_unique<TransformationWithMode>(
        Transformation::Result::Mode::kFinal,
        NewDeleteTransformation(options)));
    auto original_position = result->cursor;
    InsertOptions insert_options;
    insert_options.buffer_to_insert = buffer_to_insert;
    if (modifiers_.direction == BACKWARDS) {
      insert_options.final_position = InsertOptions::FinalPosition::kStart;
    }
    if (result->mode == Transformation::Result::Mode::kPreview) {
      insert_options.modifiers_set = {LineModifier::UNDERLINE,
                                      LineModifier::BLUE};
    }
    stack.PushBack(NewInsertBufferTransformation(std::move(insert_options)));
    if (result->mode == Transformation::Result::Mode::kPreview) {
      stack.PushBack(NewSetPositionTransformation(original_position));
    }
    stack.Apply(result);
  }

  std::unique_ptr<Transformation> Clone() const override {
    return std::make_unique<SwitchCaseTransformation>(modifiers_);
  }

 private:
  const Modifiers modifiers_;
};
}  // namespace

std::unique_ptr<Transformation> NewSwitchCaseTransformation(
    Modifiers modifiers) {
  return std::make_unique<SwitchCaseTransformation>(std::move(modifiers));
}
}  // namespace afc::editor
