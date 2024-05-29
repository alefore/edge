#include "src/paste.h"

#include "src/buffer_name.h"
#include "src/buffer_registry.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;

using afc::infrastructure::ExtendedChar;
using afc::infrastructure::FileDescriptor;
using afc::infrastructure::ProcessId;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::ToByteString;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;

namespace afc::editor {
namespace {
// TODO: Replace with insert.  Insert should be called 'type'.
class Paste : public Command {
 public:
  Paste(EditorState& editor_state) : editor_state_(editor_state) {}

  LazyString Description() const override {
    return LazyString{L"pastes the last deleted text"};
  }
  std::wstring Category() const override { return L"Edit"; }

  void ProcessInput(ExtendedChar) override {
    std::optional<gc::Ptr<OpenBuffer>> paste_buffer =
        editor_state_.buffer_registry().paste();
    if (paste_buffer == std::nullopt) {
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
      editor_state_.status().InsertError(Error(errors[current_message++]));
      if (errors[current_message].empty()) {
        current_message = 0;
      }
      return;
    }
    editor_state_
        .ForEachActiveBuffer([&editor_state = editor_state_,
                              paste_buffer =
                                  paste_buffer->ToRoot()](OpenBuffer& buffer) {
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
            buffer.status().InsertError(Error(errors[current_message++]));
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
                buffer.status().InsertError(Error(L"Unable to paste."));
                break;
              }
            }
            return futures::Past(EmptyValue());
          }
          buffer.CheckPosition();
          buffer.MaybeAdjustPositionCol();
          LOG(INFO) << "Found paste buffer, pasting...";
          return buffer.ApplyToCursors(transformation::Insert{
              .contents_to_insert = paste_buffer.ptr()->contents().snapshot(),
              .modifiers = {.insertion = editor_state.modifiers().insertion,
                            .repetitions = editor_state.repetitions()}});
        })
        .Transform([&editor_state = editor_state_](EmptyValue) {
          editor_state.ResetInsertionModifier();
          editor_state.ResetRepetitions();
          return EmptyValue();
        });
  }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};

bool tests_registration = tests::Register(
    L"Paste",
    {{.name = L"NormalPaste",
      .callback =
          [] {
            NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
            gc::Root<OpenBuffer> paste_buffer_root = OpenBuffer::New(
                {.editor = editor.value(), .name = BufferName::PasteBuffer()});
            editor->buffer_registry().SetPaste(paste_buffer_root.ptr());

            paste_buffer_root.ptr()->AppendLine(LazyString{L"Foo"});
            paste_buffer_root.ptr()->AppendLine(LazyString{L"Bar"});

            gc::Root<OpenBuffer> buffer_root =
                NewBufferForTests(editor.value());
            editor->AddBuffer(buffer_root, BuffersList::AddBufferType ::kVisit);

            OpenBuffer& buffer = buffer_root.ptr().value();
            buffer.AppendLine(LazyString{L"Quux"});
            buffer.set_position(LineColumn(LineNumber(1), ColumnNumber(2)));

            Paste(buffer.editor()).ProcessInput('x');

            LOG(INFO) << "Contents: "
                      << buffer.contents().snapshot().ToString();
            CHECK(buffer.contents().snapshot().ToString() ==
                  L"\nQu\nFoo\nBarux");
          }},
     {.name = L"PasteWithFileDescriptor", .callback = [] {
        NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
        gc::Root<OpenBuffer> paste_buffer_root = OpenBuffer::New(
            {.editor = editor.value(), .name = BufferName::PasteBuffer()});
        editor->buffer_registry().SetPaste(paste_buffer_root.ptr());

        paste_buffer_root.ptr()->AppendLine(LazyString{L"Foo"});
        paste_buffer_root.ptr()->AppendLine(LazyString{L"Bar"});

        int pipefd_out[2];
        CHECK(pipe2(pipefd_out, O_NONBLOCK) != -1);

        gc::Root<OpenBuffer> buffer_root = NewBufferForTests(editor.value());
        editor->AddBuffer(buffer_root, BuffersList::AddBufferType ::kVisit);

        OpenBuffer& buffer = buffer_root.ptr().value();
        buffer.SetInputFiles(FileDescriptor(pipefd_out[1]), FileDescriptor(-1),
                             false, std::optional<ProcessId>());
        Paste(buffer.editor()).ProcessInput('x');

        char data[1024];
        int len = read(pipefd_out[0], data, sizeof(data));
        CHECK(len != -1) << "Read failed: " << strerror(errno);
        CHECK_EQ(std::string(data, len), "\nFoo\nBar");
      }}});
}  // namespace

gc::Root<Command> NewPasteCommand(EditorState& editor_state) {
  return editor_state.gc_pool().NewRoot(MakeNonNullUnique<Paste>(editor_state));
}

}  // namespace afc::editor
