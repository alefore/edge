#include "src/run_cpp_file.h"

#include <memory>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/language/gc.h"
#include "src/line_prompt_mode.h"

namespace afc::editor {
using infrastructure::FileSystemDriver;
using language::EmptyValue;
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::PossibleError;
using language::Success;
using language::ValueOrError;
using language::lazy_string::LazyString;

namespace gc = language::gc;

futures::Value<PossibleError> RunCppFileHandler(
    EditorState& editor_state, NonNull<std::shared_ptr<LazyString>> input) {
  // TODO(easy): Honor `multiple_buffers`.
  std::optional<gc::Root<OpenBuffer>> buffer = editor_state.current_buffer();
  if (!buffer.has_value()) {
    return futures::Past(ValueOrError<EmptyValue>(Error(L"No current buffer")));
  }
  if (editor_state.structure() == StructureLine()) {
    std::optional<Line::BufferLineColumn> line_buffer =
        buffer->ptr()->current_line()->buffer_line_column();
    if (line_buffer.has_value()) {
      if (auto target = line_buffer->buffer.Lock(); target.has_value()) {
        buffer = target;
      }
    }
    editor_state.ResetModifiers();
  }

  buffer->ptr()->ResetMode();
  auto options = ResolvePathOptions::New(
      editor_state,
      std::make_shared<FileSystemDriver>(editor_state.thread_pool()));
  // TODO(easy, 2022-06-05): Get rid of ToString.
  options.path = input->ToString();
  return OnError(ResolvePath(std::move(options)),
                 [buffer, input](Error error) {
                   // TODO(easy, 2022-06-05): Get rid of ToString.
                   buffer->ptr()->status().SetWarningText(
                       L"🗱  File not found: " + input->ToString());
                   return futures::Past(error);
                 })
      .Transform([buffer, &editor_state, input](ResolvePathOutput resolved_path)
                     -> futures::Value<PossibleError> {
        using futures::IterationControlCommand;
        auto index = std::make_shared<size_t>(0);
        return futures::While([buffer, total = editor_state.repetitions(),
                               adjusted_input = resolved_path.path, index]() {
                 if (*index >= total)
                   return futures::Past(IterationControlCommand::kStop);
                 ++*index;
                 return buffer->ptr()
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

namespace {
class RunCppFileCommand : public Command {
 public:
  RunCppFileCommand(EditorState& editor_state) : editor_state_(editor_state) {}
  std::wstring Description() const override {
    return L"runs a command from a file";
  }
  std::wstring Category() const override { return L"Extensions"; }

  void ProcessInput(wint_t) override {
    if (!editor_state_.has_current_buffer()) {
      return;
    }
    auto buffer = editor_state_.current_buffer();
    CHECK(buffer.has_value());
    Prompt(
        {.editor_state = editor_state_,
         .prompt = L"cmd ",
         .history_file = HistoryFile(L"editor_commands"),
         .initial_value =
             buffer->ptr()->Read(buffer_variables::editor_commands_path),
         .handler =
             [&editor =
                  editor_state_](NonNull<std::shared_ptr<LazyString>> input) {
               return RunCppFileHandler(editor, input).ConsumeErrors([](Error) {
                 return futures::Past(EmptyValue());
               });
             },
         .cancel_handler = []() { /* Nothing. */ },
         .predictor = FilePredictor});
  }

 private:
  EditorState& editor_state_;
};
}  // namespace

NonNull<std::unique_ptr<Command>> NewRunCppFileCommand(
    EditorState& editor_state) {
  return MakeNonNullUnique<RunCppFileCommand>(editor_state);
}

}  // namespace afc::editor
