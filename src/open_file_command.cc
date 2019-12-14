#include "src/open_file_command.h"

extern "C" {
#include <sys/stat.h>
}

#include "src/buffer_variables.h"
#include "src/dirname.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/line_prompt_mode.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

namespace {

void OpenFileHandler(const wstring& name, EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.path = name;
  options.insertion_type = BuffersList::AddBufferType::kVisit;
  OpenFile(options);
}

void DrawPath(const std::shared_ptr<OpenBuffer>& buffer,
              const std::shared_ptr<const Line>& original_line,
              std::optional<PredictResults> results) {
  CHECK(buffer != nullptr);
  auto status = buffer->editor()->status();
  CHECK(status != nullptr);
  CHECK(status->GetType() == Status::Type::kPrompt);
  if (results.has_value()) {
    LOG(INFO) << "Setting context: "
              << results->predictions_buffer->Read(buffer_variables::name);
    status->set_prompt_context(results->predictions_buffer);
  }
  CHECK_EQ(buffer->lines_size(), LineNumberDelta(1));
  auto line = buffer->LineAt(LineNumber(0));
  if (original_line->ToString() != line->ToString()) {
    LOG(INFO) << "Line has changed, ignoring call to `DrawPath`.";
    return;
  }

  Line::Options output;
  for (auto i = ColumnNumber(0); i < line->EndColumn(); ++i) {
    auto c = line->get(i);
    switch (c) {
      case L'/':
      case L'.':
        output.AppendCharacter(c, LineModifierSet({LineModifier::DIM}));
        break;
      default:
        LineModifierSet modifiers;
        if (results.has_value()) {
          auto value = results.value();
          if (i.ToDelta() >= value.longest_directory_match) {
            if (value.found_exact_match) {
              modifiers.insert(LineModifier::BOLD);
            }
            if (value.matches == 0 && i.ToDelta() >= value.longest_prefix) {
              modifiers.insert(LineModifier::RED);
            } else if (value.matches == 1) {
              modifiers.insert(LineModifier::GREEN);
            } else if (value.common_prefix.has_value() &&
                       line->EndColumn() <
                           ColumnNumber(value.common_prefix.value().size())) {
              modifiers.insert(LineModifier::YELLOW);
            }
          }
        }
        output.AppendCharacter(c, modifiers);
    }
  }
  buffer->AppendRawLine(Line::New(std::move(output)));
  buffer->EraseLines(LineNumber(0), LineNumber(1));

  CHECK_EQ(buffer->lines_size(), LineNumberDelta(1));
}

void AdjustPath(const std::shared_ptr<OpenBuffer>& buffer) {
  CHECK(buffer != nullptr);
  CHECK_EQ(buffer->lines_size(), LineNumberDelta(1));
  auto line = buffer->LineAt(LineNumber(0));

  PredictOptions options;
  options.editor_state = buffer->editor();
  options.predictor = FilePredictor;
  options.status = buffer->editor()->status();
  options.callback = [buffer, line](PredictResults results) {
    VLOG(5) << "Prediction results: " << results;
    DrawPath(buffer, line, results);
  };
  Predict(std::move(options));
  DrawPath(buffer, line, std::nullopt);
}
}  // namespace

std::unique_ptr<Command> NewOpenFileCommand() {
  PromptOptions options;
  options.prompt = L"<";
  options.history_file = L"files";
  options.handler = OpenFileHandler;
  options.cancel_handler = [](EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer != nullptr) {
      buffer->ResetMode();
    }
  };
  options.predictor = FilePredictor;
  options.change_notifier = AdjustPath;
  return NewLinePromptCommand(
      L"loads a file", [options](EditorState* editor_state) {
        PromptOptions options_copy = options;
        auto buffer = editor_state->current_buffer();

        if (buffer != nullptr) {
          wstring path = buffer->Read(buffer_variables::path);
          struct stat stat_buffer;
          if (stat(ToByteString(path).c_str(), &stat_buffer) == -1 ||
              !S_ISDIR(stat_buffer.st_mode)) {
            LOG(INFO) << "Taking dirname for prompt: " << path;
            path = Dirname(path);
          }
          if (path == L".") {
            path = L"";
          } else if (path.empty() || *path.rbegin() != L'/') {
            path += L'/';
          }
          options_copy.initial_value = path;
        }
        return options_copy;
      });
}

}  // namespace editor
}  // namespace afc
