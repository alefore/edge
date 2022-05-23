#include "src/paste.h"

#include "src/buffer_name.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/language/gc.h"

namespace afc::editor {
namespace {
using language::EmptyValue;
using language::MakeNonNullUnique;
using language::ToByteString;

namespace gc = language::gc;

// TODO: Replace with insert.  Insert should be called 'type'.
class Paste : public Command {
 public:
  Paste(EditorState& editor_state) : editor_state_(editor_state) {}

  std::wstring Description() const override {
    return L"pastes the last deleted text";
  }
  std::wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t) {
    auto it = editor_state_.buffers()->find(BufferName::PasteBuffer());
    if (it == editor_state_.buffers()->end()) {
      const static std::wstring errors[] = {
          L"No text to paste.",
          L"Try deleting something first.",
          L"You can't paste what you haven't deleted.",
          L"First delete; then paste.",
          L"I have nothing to paste.",
          L"The paste buffer is empty.",
          L"There's nothing to paste.",
          L"Nope.",
          L"Let's see, is there's something to paste? Nope.",
          L"The paste buffer is desolate.",
          L"Paste what?",
          L"I'm sorry, Dave, I'm afraid I can't do that.",
          L"",
      };
      static int current_message = 0;
      editor_state_.status().SetWarningText(errors[current_message++]);
      if (errors[current_message].empty()) {
        current_message = 0;
      }
      return;
    }
    gc::Root<OpenBuffer> paste_buffer = it->second;
    editor_state_
        .ForEachActiveBuffer([&editor_state = editor_state_,
                              paste_buffer](OpenBuffer& buffer) {
          if (&paste_buffer.ptr().value() == &buffer) {
            const static std::wstring errors[] = {
                L"You shall not paste into the paste buffer.",
                L"Nope.",
                L"Bad things would happen if you pasted into the buffer.",
                L"There could be endless loops if you pasted into this "
                L"buffer.",
                L"This is not supported.",
                L"Go to a different buffer first?",
                L"The paste buffer is not for pasting into.",
                L"This editor is too important for me to allow you to "
                L"jeopardize it.",
                L"",
            };
            static int current_message = 0;
            buffer.status().SetWarningText(errors[current_message++]);
            if (errors[current_message].empty()) {
              current_message = 0;
            }
            return futures::Past(EmptyValue());
          }
          if (buffer.fd() != nullptr) {
            string text = ToByteString(paste_buffer.ptr()->ToString());
            for (size_t i = 0; i < editor_state.repetitions(); i++) {
              if (write(buffer.fd()->fd().read(), text.c_str(), text.size()) ==
                  -1) {
                buffer.status().SetWarningText(L"Unable to paste.");
                break;
              }
            }
            return futures::Past(EmptyValue());
          }
          buffer.CheckPosition();
          buffer.MaybeAdjustPositionCol();
          return buffer.ApplyToCursors(transformation::Insert{
              .contents_to_insert = paste_buffer.ptr()->contents().copy(),
              .modifiers = {.insertion = editor_state.modifiers().insertion,
                            .repetitions = editor_state.repetitions()}});
        })
        .Transform([&editor_state = editor_state_](EmptyValue) {
          editor_state.ResetInsertionModifier();
          editor_state.ResetRepetitions();
          return EmptyValue();
        });
  }

 private:
  EditorState& editor_state_;
};
}  // namespace

language::NonNull<std::unique_ptr<Command>> NewPasteCommand(
    EditorState& editor_state) {
  return MakeNonNullUnique<Paste>(editor_state);
}

}  // namespace afc::editor
