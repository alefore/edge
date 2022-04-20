#include "src/run_cpp_file.h"

#include <memory>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/line_prompt_mode.h"

namespace afc::editor {
using infrastructure::FileSystemDriver;
using language::EmptyValue;
using language::Error;
using language::PossibleError;
using language::Success;
using language::ValueOrError;

namespace {
class RunCppFileCommand : public Command {
 public:
  RunCppFileCommand(EditorState& editor_state) : editor_state_(editor_state) {}
  wstring Description() const override { return L"runs a command from a file"; }
  wstring Category() const override { return L"Extensions"; }

  void ProcessInput(wint_t) override {
    if (!editor_state_.has_current_buffer()) {
      return;
    }
    auto buffer = editor_state_.current_buffer();
    CHECK(buffer != nullptr);
    Prompt(
        {.editor_state = editor_state_,
         .prompt = L"cmd ",
         .history_file = HistoryFile(L"editor_commands"),
         .initial_value = buffer->Read(buffer_variables::editor_commands_path),
         .handler =
             [&editor = editor_state_](const wstring& input) {
               futures::Future<EmptyValue> output;
               RunCppFileHandler(input, editor)
                   .SetConsumer([consumer = std::move(output.consumer)](
                                    ValueOrError<EmptyValue>) {
                     consumer(EmptyValue());
                   });
               return std::move(output.value);
             },
         .cancel_handler = []() { /* Nothing. */ },
         .predictor = FilePredictor});
  }

 private:
  EditorState& editor_state_;
};
}  // namespace

futures::Value<PossibleError> RunCppFileHandler(const wstring& input,
                                                EditorState& editor_state) {
  // TODO(easy): Honor `multiple_buffers`.
  auto buffer = editor_state.current_buffer();
  if (buffer == nullptr) {
    return futures::Past(ValueOrError<EmptyValue>(Error(L"No current buffer")));
  }
  if (editor_state.structure() == StructureLine()) {
    auto target = buffer->GetBufferFromCurrentLine();
    if (target != nullptr) {
      buffer = target;
    }
    editor_state.ResetModifiers();
  }

  buffer->ResetMode();
  auto options = ResolvePathOptions::New(
      editor_state,
      std::make_shared<FileSystemDriver>(editor_state.thread_pool()));
  options.path = input;
  return OnError(ResolvePath(std::move(options)),
                 [buffer, input](Error error) {
                   buffer->status().SetWarningText(L"🗱  File not found: " +
                                                   input);
                   return error;
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
                 return buffer->EvaluateFile(adjusted_input)
                     .Transform([](std::unique_ptr<Value>) {
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

std::unique_ptr<Command> NewRunCppFileCommand(EditorState& editor_state) {
  return std::make_unique<RunCppFileCommand>(editor_state);
}

}  // namespace afc::editor
