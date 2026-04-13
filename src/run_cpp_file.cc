#include "src/run_cpp_file.h"

#include <memory>

#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/file_predictor.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/single_line.h"
#include "src/line_prompt_mode.h"
#include "src/vm/escape.h"

namespace gc = afc::language::gc;

using afc::infrastructure::ExtendedChar;
using afc::infrastructure::FileSystemDriver;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::OutgoingLink;
using afc::vm::EscapedString;

namespace afc::editor {
namespace {
futures::Value<PossibleError> RunCppFileHandler(EditorState& editor_state,
                                                SingleLine input) {
  // TODO(easy): Honor `multiple_buffers`.
  std::optional<gc::Root<OpenBuffer>> buffer = editor_state.current_buffer();
  if (!buffer.has_value()) {
    return futures::Past(
        ValueOrError<EmptyValue>(Error{LazyString{L"No current buffer"}}));
  }
  if (editor_state.structure() == Structure::kLine) {
    if (std::optional<OutgoingLink> outgoing_link =
            buffer->ptr()->CurrentLine().outgoing_link();
        outgoing_link.has_value())
      if (std::optional<gc::Root<OpenBuffer>> link_buffer =
              editor_state.buffer_registry().Find(
                  BufferFileId{outgoing_link->path});
          link_buffer.has_value())
        buffer = std::move(link_buffer);
    editor_state.ResetModifiers();
  }

  buffer->ptr()->ResetMode();
  ResolvePathOptions options = ResolvePathOptions::New(
      editor_state,
      MakeNonNullShared<FileSystemDriver>(editor_state.thread_pool()));
  options.path = input.read();
  return ResolvePath(std::move(options))
      .Transform([buffer, input, &editor_state](ResolvePathOutput resolved_path)
                     -> futures::Value<PossibleError> {
        if (resolved_path.entries.empty()) {
          Error error{LazyString{L"🗱  File not found: "} + input.read()};
          buffer->ptr()->status().InsertError(error);
          return futures::Past(error);
        }
        using futures::IterationControlCommand;
        auto index = MakeNonNullShared<size_t>(0);
        // TODO(P1, multiple-files, 2026-04-09, trivial): Honor all entries, not
        // just the first one.
        return futures::While([buffer, total = editor_state.repetitions(),
                               adjusted_input = resolved_path.entries[0].path,
                               index]() {
                 if (index.value() >= total)
                   return futures::Past(IterationControlCommand::kStop);
                 ++index.value();
                 return buffer->ptr()
                     ->execution_context()
                     ->EvaluateFile(adjusted_input)
                     .Transform([](gc::Root<vm::Value>) {
                       return Success(IterationControlCommand::kContinue);
                     })
                     .ConsumeErrors([](Error) {
                       return futures::Past(IterationControlCommand::kStop);
                     });
               })
            .Transform([&editor_state](IterationControlCommand) {
              editor_state.ResetRepetitions();
              return Success();
            });
      });
}

class RunCppFileCommand : public Command {
 public:
  RunCppFileCommand(EditorState& editor_state) : editor_state_(editor_state) {}
  LazyString Description() const override {
    return LazyString{L"runs a command from a file"};
  }
  CommandCategory Category() const override {
    return CommandCategory::kExtensions();
  }

  void ProcessInput(ExtendedChar) override {
    if (!editor_state_.has_current_buffer()) {
      return;
    }
    auto buffer = editor_state_.current_buffer();
    CHECK(buffer.has_value());
    Prompt(
        {.editor_state = editor_state_,
         .prompt = LineBuilder{SingleLine{LazyString{L"cmd "}}}.Build(),
         .history_file =
             HistoryFile{NON_EMPTY_SINGLE_LINE_CONSTANT(L"editor_commands")},
         .initial_value = Line{EscapedString::FromString(
                                   buffer->ptr()->Read(
                                       buffer_variables::editor_commands_path))
                                   .EscapedRepresentation()},
         .handler =
             [&editor = editor_state_](SingleLine input) {
               return RunCppFileHandler(editor, input).ConsumeErrors([](Error) {
                 return futures::Past(EmptyValue());
               });
             },
         .cancel_handler = []() { /* Nothing. */ },
         .predictor = GetFilePredictor(FilePredictorOptions{})});
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};
}  // namespace

gc::Root<Command> NewRunCppFileCommand(EditorState& editor_state) {
  return editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<RunCppFileCommand>(editor_state));
}

}  // namespace afc::editor
