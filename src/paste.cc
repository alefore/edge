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
    std::optional<gc::Root<OpenBuffer>> paste_buffer =
        editor_state_.buffer_registry().Find(PasteBuffer{});
    if (paste_buffer == std::nullopt) {
      LOG(INFO) << "Attempted to paste without a paste buffer.";
      const static Error errors[] = {
          Error{LazyString{L"No text to paste."}},
          Error{LazyString{L"Try deleting something first."}},
          Error{LazyString{L"You can't paste what you haven't deleted."}},
          Error{LazyString{L"First delete; then paste."}},
          Error{LazyString{L"I have nothing to paste."}},
          Error{LazyString{L"The paste buffer is empty."}},
          Error{LazyString{L"There's nothing to paste."}},
          Error{LazyString{L"Nope."}},
          Error{LazyString{L"Let's see, is there's something to paste? Nope."}},
          Error{LazyString{L"The paste buffer is desolate."}},
          Error{LazyString{L"Paste what?"}},
          Error{LazyString{L"I'm sorry, Dave, I'm afraid I can't do that."}},
          Error{LazyString{}},
      };
      static int current_message = 0;
      editor_state_.status().InsertError(errors[current_message++]);
      if (errors[current_message].read().IsEmpty()) {
        current_message = 0;
      }
      return;
    }
    editor_state_
        .ForEachActiveBuffer([&editor_state = editor_state_,
                              paste_buffer = std::move(paste_buffer.value())](
                                 OpenBuffer& buffer) {
          if (&paste_buffer.ptr().value() == &buffer) {
            LOG(INFO) << "Attempted to paste into paste buffer.";
            const static Error errors[] = {
                Error{
                    LazyString{L"You shall not paste into the paste buffer."}},
                Error{LazyString{L"Nope."}},
                Error{LazyString{
                    L"Bad things would happen if you pasted into the buffer."}},
                Error{LazyString{
                    L"There could be endless loops if you pasted into this "
                    L"buffer."}},
                Error{LazyString{L"This is not supported."}},
                Error{LazyString{L"Go to a different buffer first?"}},
                Error{LazyString{L"The paste buffer is not for pasting into."}},
                Error{LazyString{
                    L"This editor is too important for me to allow you "
                    L"to jeopardize it."}},
                Error{LazyString{}},
            };
            static int current_message = 0;
            buffer.status().InsertError(errors[current_message++]);
            if (errors[current_message].read().IsEmpty()) {
              current_message = 0;
            }
            return futures::Past(EmptyValue());
          }
          if (buffer.fd() != nullptr) {
            std::string text = paste_buffer.ptr()->ToString().ToBytes();
            VLOG(4) << "Writing to fd: " << text;
            for (size_t i = 0; i < editor_state.repetitions().value_or(1);
                 i++) {
              if (write(buffer.fd()->fd().read(), text.c_str(), text.size()) ==
                  -1) {
                buffer.status().InsertError(
                    Error{LazyString{L"Unable to paste."}});
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
            std::optional<gc::Root<OpenBuffer>> paste_buffer_root =
                OpenBuffer::New(OpenBuffer::Options{.editor = editor.value(),
                                                    .name = PasteBuffer()});
            editor->buffer_registry().Add(PasteBuffer{},
                                          paste_buffer_root->ptr().ToWeakPtr());

            paste_buffer_root->ptr()->AppendLine(LazyString{L"Foo"});
            paste_buffer_root->ptr()->AppendLine(LazyString{L"Bar"});
            paste_buffer_root = std::nullopt;
            editor->gc_pool().Collect();

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
        std::optional<gc::Root<OpenBuffer>> paste_buffer_root =
            OpenBuffer::New(OpenBuffer::Options{.editor = editor.value(),
                                                .name = PasteBuffer()});
        editor->buffer_registry().Add(PasteBuffer{},
                                      paste_buffer_root->ptr().ToWeakPtr());

        paste_buffer_root->ptr()->AppendLine(LazyString{L"Foo"});
        paste_buffer_root->ptr()->AppendLine(LazyString{L"Bar"});
        paste_buffer_root = std::nullopt;
        editor->gc_pool().Collect();

        int pipefd_out[2];
        CHECK(pipe2(pipefd_out, O_NONBLOCK) != -1);

        gc::Root<OpenBuffer> buffer_root = NewBufferForTests(editor.value());
        editor->AddBuffer(buffer_root, BuffersList::AddBufferType ::kVisit);

        OpenBuffer& buffer = buffer_root.ptr().value();
        buffer.SetInputFiles(FileDescriptor(pipefd_out[1]), std::nullopt, false,
                             std::optional<ProcessId>());
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
