#include "src/paste.h"

#include "src/buffer_name.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
using infrastructure::FileDescriptor;
using language::EmptyValue;
using language::MakeNonNullUnique;
using language::ToByteString;
using language::lazy_string::ColumnNumber;
using language::lazy_string::NewLazyString;
using language::text::LineColumn;
using language::text::LineNumber;

namespace gc = language::gc;

// TODO: Replace with insert.  Insert should be called 'type'.
class Paste : public Command {
 public:
  Paste(EditorState& editor_state) : editor_state_(editor_state) {}

  std::wstring Description() const override {
    return L"pastes the last deleted text";
  }
  std::wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t) override {
    auto it = editor_state_.buffers()->find(BufferName::PasteBuffer());
    if (it == editor_state_.buffers()->end()) {
      LOG(INFO) << "Attempted to paste without a paste buffer.";
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
            LOG(INFO) << "Attempted to paste into paste buffer.";
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
            std::string text = ToByteString(paste_buffer.ptr()->ToString());
            VLOG(4) << "Writing to fd: " << text;
            for (size_t i = 0; i < editor_state.repetitions().value_or(1);
                 i++) {
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
          LOG(INFO) << "Found paste buffer, pasting...";
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

bool tests_registration = tests::Register(
    L"Paste",
    {{.name = L"NormalPaste",
      .callback =
          [] {
            gc::Root<OpenBuffer> paste_buffer_root =
                OpenBuffer::New({.editor = EditorForTests(),
                                 .name = BufferName::PasteBuffer()});
            EditorForTests().buffers()->insert_or_assign(
                paste_buffer_root.ptr()->name(), paste_buffer_root);

            paste_buffer_root.ptr()->AppendLine(NewLazyString(L"Foo"));
            paste_buffer_root.ptr()->AppendLine(NewLazyString(L"Bar"));

            gc::Root<OpenBuffer> buffer_root = NewBufferForTests();
            EditorForTests().AddBuffer(buffer_root,
                                       BuffersList::AddBufferType ::kVisit);

            OpenBuffer& buffer = buffer_root.ptr().value();
            buffer.AppendLine(NewLazyString(L"Quux"));
            buffer.set_position(LineColumn(LineNumber(1), ColumnNumber(2)));

            Paste(buffer.editor()).ProcessInput('x');

            LOG(INFO) << "Contents: " << buffer.contents().ToString();
            CHECK(buffer.contents().ToString() == L"\nQu\nFoo\nBarux");
          }},
     {.name = L"PasteWithFileDescriptor", .callback = [] {
        gc::Root<OpenBuffer> paste_buffer_root = OpenBuffer::New(
            {.editor = EditorForTests(), .name = BufferName::PasteBuffer()});
        EditorForTests().buffers()->insert_or_assign(
            paste_buffer_root.ptr()->name(), paste_buffer_root);

        paste_buffer_root.ptr()->AppendLine(NewLazyString(L"Foo"));
        paste_buffer_root.ptr()->AppendLine(NewLazyString(L"Bar"));

        int pipefd_out[2];
        CHECK(pipe2(pipefd_out, O_NONBLOCK) != -1);

        gc::Root<OpenBuffer> buffer_root = NewBufferForTests();
        EditorForTests().AddBuffer(buffer_root,
                                   BuffersList::AddBufferType ::kVisit);

        OpenBuffer& buffer = buffer_root.ptr().value();
        buffer.SetInputFiles(FileDescriptor(pipefd_out[1]), FileDescriptor(-1),
                             false, 0);
        Paste(buffer.editor()).ProcessInput('x');

        char data[1024];
        int len = read(pipefd_out[0], data, sizeof(data));
        CHECK(len != -1) << "Read failed: " << strerror(errno);
        CHECK_EQ(std::string(data, len), "\nFoo\nBar");
      }}});
}  // namespace

language::NonNull<std::unique_ptr<Command>> NewPasteCommand(
    EditorState& editor_state) {
  return MakeNonNullUnique<Paste>(editor_state);
}

}  // namespace afc::editor
