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

futures::Value<EmptyValue> OpenFileHandler(const wstring& name,
                                           EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.path = name;
  options.insertion_type = BuffersList::AddBufferType::kVisit;
  OpenFile(options);
  return futures::Past(EmptyValue());
}

std::shared_ptr<OpenBuffer> StatusContext(EditorState* editor,
                                          const PredictResults& results,
                                          const LazyString& line) {
  if (results.found_exact_match) {
    OpenFileOptions open_file_options;
    open_file_options.editor_state = editor;
    open_file_options.path = line.ToString();
    open_file_options.insertion_type = BuffersList::AddBufferType::kIgnore;
    open_file_options.ignore_if_not_found = true;
    if (auto result = OpenFile(open_file_options);
        result != editor->buffers()->end()) {
      return result->second;
    }
  }
  if (results.predictions_buffer->lines_size() == LineNumberDelta(1) &&
      results.predictions_buffer->LineAt(LineNumber())->empty()) {
    return nullptr;
  }
  LOG(INFO) << "Setting context: "
            << results.predictions_buffer->Read(buffer_variables::name);
  return results.predictions_buffer;
}

ColorizePromptOptions DrawPath(EditorState* editor,
                               const std::shared_ptr<LazyString>& line,
                               PredictResults results) {
  ColorizePromptOptions output;
  output.context = StatusContext(editor, results, *line);

  for (auto i = ColumnNumber(0); i < ColumnNumber(0) + line->size(); ++i) {
    LineModifierSet modifiers;
    switch (line->get(i)) {
      case L'/':
      case L'.':
        modifiers.insert(LineModifier::DIM);
        break;
      default:
        if (i.ToDelta() >= results.longest_directory_match) {
          if (results.found_exact_match) {
            modifiers.insert(LineModifier::BOLD);
          }
          if (results.matches == 0 && i.ToDelta() >= results.longest_prefix) {
            modifiers.insert(LineModifier::RED);
          } else if (results.matches == 1) {
            modifiers.insert(LineModifier::GREEN);
          } else if (results.common_prefix.has_value() &&
                     ColumnNumber() + line->size() <
                         ColumnNumber(results.common_prefix.value().size())) {
            modifiers.insert(LineModifier::YELLOW);
          }
        }
    }
    output.tokens.push_back(TokenAndModifiers{
        .token = {.begin = i, .end = i.next()}, .modifiers = modifiers});
  }
  return output;
}

futures::Value<ColorizePromptOptions> AdjustPath(
    EditorState* editor, const std::shared_ptr<LazyString>& line) {
  PredictOptions options;
  options.editor_state = editor;
  options.predictor = FilePredictor;
  options.status = editor->status();
  options.source_buffers = editor->active_buffers();
  options.text = line->ToString();
  options.input_selection_structure = StructureLine();
  return futures::Transform(
      Predict(std::move(options)),
      [editor, line](std::optional<PredictResults> results) {
        if (!results.has_value()) return ColorizePromptOptions{};
        return DrawPath(editor, line, std::move(results.value()));
      });
}
}  // namespace

std::unique_ptr<Command> NewOpenFileCommand(EditorState* editor) {
  PromptOptions options;
  options.prompt = L"<";
  options.prompt_contents_type = L"path";
  options.history_file = L"files";
  options.handler = OpenFileHandler;
  options.cancel_handler = [](EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer != nullptr) {
      buffer->ResetMode();
    }
  };
  options.predictor = FilePredictor;
  options.colorize_options_provider =
      [editor](const std::shared_ptr<LazyString>& line) {
        return AdjustPath(editor, line);
      };
  return NewLinePromptCommand(
      L"loads a file", [options](EditorState* editor_state) {
        PromptOptions options_copy = options;
        options_copy.editor_state = editor_state;
        options_copy.source_buffers = editor_state->active_buffers();
        if (!options_copy.source_buffers.empty()) {
          auto buffer = options_copy.source_buffers[0];
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
