#include "src/open_file_command.h"

#include "src/language/wstring.h"

extern "C" {
#include <sys/stat.h>
}

#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/line_prompt_mode.h"
#include "src/tests/tests.h"

namespace afc {
namespace editor {

namespace {
using concurrent::Notification;
using infrastructure::Path;
using language::EmptyValue;
using language::Error;
using language::NonNull;
using language::Success;
using language::ToByteString;

futures::Value<EmptyValue> OpenFileHandler(const wstring& name,
                                           EditorState& editor_state) {
  OpenOrCreateFile(
      OpenFileOptions{.editor_state = editor_state,
                      .path = Path::FromString(name).AsOptional(),
                      .insertion_type = BuffersList::AddBufferType::kVisit});
  return futures::Past(EmptyValue());
}

// Returns the buffer to show for context, or nullptr.
futures::Value<std::shared_ptr<OpenBuffer>> StatusContext(
    EditorState& editor, const PredictResults& results,
    const LazyString& line) {
  futures::Value<std::shared_ptr<OpenBuffer>> output = futures::Past(nullptr);
  if (results.found_exact_match) {
    auto path = Path::FromString(line.ToString());
    if (path.IsError()) {
      return futures::Past(nullptr);
    }
    output = OpenFileIfFound(
                 OpenFileOptions{
                     .editor_state = editor,
                     .path = path.value(),
                     .insertion_type = BuffersList::AddBufferType::kIgnore})
                 .Transform([](NonNull<std::shared_ptr<OpenBuffer>> buffer) {
                   return Success(buffer.get_shared());
                 })
                 .ConsumeErrors([](Error) { return futures::Past(nullptr); });
  }
  return output.Transform([results](std::shared_ptr<OpenBuffer> buffer)
                              -> std::shared_ptr<OpenBuffer> {
    if (buffer != nullptr) return buffer;
    if (results.predictions_buffer->lines_size() == LineNumberDelta(1) &&
        results.predictions_buffer->contents().at(LineNumber())->empty()) {
      return nullptr;
    }
    LOG(INFO) << "Setting context: "
              << results.predictions_buffer->Read(buffer_variables::name);
    return results.predictions_buffer.get_shared();
  });
}

futures::Value<ColorizePromptOptions> DrawPath(
    EditorState& editor, const NonNull<std::shared_ptr<LazyString>>& line,
    PredictResults results) {
  return StatusContext(editor, results, line.value())
      .Transform([line, results](std::shared_ptr<OpenBuffer> context_buffer) {
        ColorizePromptOptions output;
        output.context = context_buffer;

        for (auto i = ColumnNumber(0); i < ColumnNumber(0) + line->size();
             ++i) {
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
                if (results.matches == 0 &&
                    i.ToDelta() >= results.longest_prefix) {
                  modifiers.insert(LineModifier::RED);
                } else if (results.matches == 1) {
                  modifiers.insert(LineModifier::GREEN);
                } else if (results.common_prefix.has_value() &&
                           ColumnNumber() + line->size() <
                               ColumnNumber(
                                   results.common_prefix.value().size())) {
                  modifiers.insert(LineModifier::YELLOW);
                }
              }
          }
          output.tokens.push_back(TokenAndModifiers{
              .token = {.begin = i, .end = i.next()}, .modifiers = modifiers});
        }
        return output;
      });
}

futures::Value<ColorizePromptOptions> AdjustPath(
    EditorState& editor, const NonNull<std::shared_ptr<LazyString>>& line,
    std::unique_ptr<ProgressChannel> progress_channel,
    NonNull<std::shared_ptr<Notification>> abort_notification) {
  CHECK(progress_channel != nullptr);
  return Predict(PredictOptions{
                     .editor_state = editor,
                     .predictor = FilePredictor,
                     .text = line->ToString(),
                     .input_selection_structure = StructureLine(),
                     .source_buffers = editor.active_buffers(),
                     .progress_channel = std::move(progress_channel),
                     .abort_notification = std::move(abort_notification)})
      .Transform([&editor, line](std::optional<PredictResults> results) {
        if (!results.has_value()) return futures::Past(ColorizePromptOptions{});
        return DrawPath(editor, line, std::move(results.value()));
      });
}

std::wstring GetInitialPromptValue(std::optional<unsigned int> repetitions,
                                   std::wstring buffer_path) {
  auto path_or_error = Path::FromString(buffer_path);
  if (path_or_error.IsError()) return L"";
  auto path = path_or_error.value();
  struct stat stat_buffer;
  // TODO(blocking): Use FileSystemDriver here!
  if (stat(ToByteString(path.read()).c_str(), &stat_buffer) == -1 ||
      !S_ISDIR(stat_buffer.st_mode)) {
    LOG(INFO) << "Taking dirname for prompt: " << path;
    auto dir_or_error = path.Dirname();
    if (!dir_or_error.IsError()) {
      path = dir_or_error.value();
    }
  }
  if (path == Path::LocalDirectory()) {
    return L"";
  }
  if (repetitions.has_value()) {
    if (repetitions.value() == 0) {
      return L"";
    }
    auto split = path.DirectorySplit();
    if (!split.IsError() && split.value().size() > repetitions.value()) {
      std::optional<Path> output_path;
      switch (path.GetRootType()) {
        case Path::RootType::kAbsolute:
          output_path = Path::Root();
          break;
        case Path::RootType::kRelative:
          break;
      }
      for (size_t i = 0; i < repetitions.value(); i++) {
        auto part = Path(split.value().front());
        split.value().pop_front();
        if (output_path.has_value()) {
          output_path = Path::Join(output_path.value(), part);
        } else {
          output_path = part;
        }
      }
      path = output_path.value();
    }
  }
  return path.read() + L"/";
}

const bool get_initial_prompt_value_tests_registration = tests::Register(
    L"GetInitialPromptValue",
    {
        {.name = L"EmptyNoRepetitions",
         .callback = [] { CHECK(GetInitialPromptValue({}, L"") == L""); }},
        {.name = L"EmptyRepetitions",
         .callback = [] { CHECK(GetInitialPromptValue(5, L"") == L""); }},
        {.name = L"NoRepetitionsRelative",
         .callback =
             [] { CHECK(GetInitialPromptValue({}, L"foo/bar") == L"foo/"); }},
        {.name = L"NoRepetitionsAbsolute",
         .callback =
             [] { CHECK(GetInitialPromptValue({}, L"/foo/bar") == L"/foo/"); }},
        {.name = L"ZeroRepetitionsRelative",
         .callback =
             [] { CHECK(GetInitialPromptValue(0, L"foo/bar") == L""); }},
        {.name = L"ZeroRepetitionsAbsolute",
         .callback =
             [] { CHECK(GetInitialPromptValue(0, L"/foo/bar") == L""); }},
        {.name = L"LowRepetitionsRelative",
         .callback =
             [] {
               CHECK(GetInitialPromptValue(2, L"a0/b1/c2/d3") == L"a0/b1/");
             }},
        {.name = L"LowRepetitionsAbsolute",
         .callback =
             [] {
               CHECK(GetInitialPromptValue(2, L"/a0/b1/c2/d3") == L"/a0/b1/");
             }},
        {.name = L"BoundaryRepetitionsRelative",
         .callback =
             [] {
               CHECK(GetInitialPromptValue(3, L"a0/b1/c2/d3") == L"a0/b1/c2/");
             }},
        {.name = L"BoundaryRepetitionsAbsolute",
         .callback =
             [] {
               CHECK(GetInitialPromptValue(3, L"/a0/b1/c2/d3") ==
                     L"/a0/b1/c2/");
             }},
        {.name = L"HighRepetitionsRelative",
         .callback =
             [] {
               CHECK(GetInitialPromptValue(40, L"a0/b1/c2/d3") == L"a0/b1/c2/");
             }},
        {.name = L"HighRepetitionsAbsolute",
         .callback =
             [] {
               CHECK(GetInitialPromptValue(40, L"/a0/b1/c2/d3") ==
                     L"/a0/b1/c2/");
             }},
    });
}  // namespace

NonNull<std::unique_ptr<Command>> NewOpenFileCommand(EditorState& editor) {
  return NewLinePromptCommand(editor, L"loads a file", [&editor]() {
    auto source_buffers = editor.active_buffers();
    return PromptOptions{
        .editor_state = editor,
        .prompt = L"<",
        .prompt_contents_type = L"path",
        .history_file = HistoryFileFiles(),
        .initial_value =
            source_buffers.empty()
                ? L""
                : GetInitialPromptValue(
                      editor.modifiers().repetitions,
                      source_buffers[0]->Read(buffer_variables::path)),
        .colorize_options_provider =
            [&editor](
                const NonNull<std::shared_ptr<LazyString>>& line,
                std::unique_ptr<ProgressChannel> progress_channel,
                NonNull<std::shared_ptr<Notification>> abort_notification) {
              return AdjustPath(editor, line, std::move(progress_channel),
                                std::move(abort_notification));
            },
        .handler =
            [&editor](const std::wstring input) {
              return OpenFileHandler(input, editor);
            },
        .cancel_handler =
            [&editor]() {
              if (auto buffer = editor.current_buffer(); buffer != nullptr) {
                buffer->ResetMode();
              }
            },
        .predictor = FilePredictor,
        .source_buffers = source_buffers};
  });
}

}  // namespace editor
}  // namespace afc
