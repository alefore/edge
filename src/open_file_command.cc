#include "src/open_file_command.h"

#include "src/language/wstring.h"

extern "C" {
#include <sys/stat.h>
}

#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/futures/delete_notification.h"
#include "src/infrastructure/dirname.h"
#include "src/language/overload.h"
#include "src/line_prompt_mode.h"
#include "src/tests/tests.h"

namespace afc::editor {
namespace {
using concurrent::Notification;
using futures::DeleteNotification;
using infrastructure::Path;
using infrastructure::PathComponent;
using language::EmptyValue;
using language::Error;
using language::IgnoreErrors;
using language::NonNull;
using language::overload;
using language::Success;
using language::ToByteString;
using language::ValueOrError;
using language::VisitPointer;
using language::lazy_string::ColumnNumber;
using language::lazy_string::LazyString;

namespace gc = language::gc;

futures::Value<EmptyValue> OpenFileHandler(
    EditorState& editor_state, NonNull<std::shared_ptr<LazyString>> name) {
  // TODO(easy, 2022-06-05): Get rid of ToString.
  OpenOrCreateFile(
      OpenFileOptions{.editor_state = editor_state,
                      .path = OptionalFrom(Path::FromString(name->ToString())),
                      .insertion_type = BuffersList::AddBufferType::kVisit});
  return futures::Past(EmptyValue());
}

// Returns the buffer to show for context, or nullptr.
futures::Value<std::optional<gc::Root<OpenBuffer>>> StatusContext(
    EditorState& editor, const PredictResults& results,
    const LazyString& line) {
  futures::Value<std::optional<gc::Root<OpenBuffer>>> output =
      futures::Past(std::optional<gc::Root<OpenBuffer>>());
  if (results.found_exact_match) {
    ValueOrError<Path> path_or_error = Path::FromString(line.ToString());
    Path* path = std::get_if<Path>(&path_or_error);
    if (path == nullptr) {
      return futures::Past(std::optional<gc::Root<OpenBuffer>>());
    }
    output = OpenFileIfFound(
                 OpenFileOptions{
                     .editor_state = editor,
                     .path = *path,
                     .insertion_type = BuffersList::AddBufferType::kIgnore})
                 .Transform([](gc::Root<OpenBuffer> buffer) {
                   return Success(std::optional<gc::Root<OpenBuffer>>(buffer));
                 })
                 .ConsumeErrors([](Error) {
                   return futures::Past(std::optional<gc::Root<OpenBuffer>>());
                 });
  }
  return output.Transform([results](std::optional<gc::Root<OpenBuffer>> buffer)
                              -> std::optional<gc::Root<OpenBuffer>> {
    if (buffer.has_value()) return buffer;
    if (results.predictions_buffer.ptr()->lines_size() == LineNumberDelta(1) &&
        results.predictions_buffer.ptr()
            ->contents()
            .at(LineNumber())
            ->empty()) {
      return std::nullopt;
    }
    LOG(INFO) << "Setting context: "
              << results.predictions_buffer.ptr()->Read(buffer_variables::name);
    return results.predictions_buffer;
  });
}

futures::Value<ColorizePromptOptions> DrawPath(
    EditorState& editor, const NonNull<std::shared_ptr<LazyString>>& line,
    PredictResults results) {
  return StatusContext(editor, results, line.value())
      .Transform([line,
                  results](std::optional<gc::Root<OpenBuffer>> context_buffer) {
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
    NonNull<std::unique_ptr<ProgressChannel>> progress_channel,
    DeleteNotification::Value abort_value) {
  return Predict(PredictOptions{.editor_state = editor,
                                .predictor = FilePredictor,
                                .text = line->ToString(),
                                .input_selection_structure = StructureLine(),
                                .source_buffers = editor.active_buffers(),
                                .progress_channel =
                                    std::move(progress_channel.get_unique()),
                                .abort_value = std::move(abort_value)})
      .Transform([&editor, line](std::optional<PredictResults> results) {
        if (!results.has_value()) return futures::Past(ColorizePromptOptions{});
        return DrawPath(editor, line, std::move(results.value()));
      });
}

std::wstring GetInitialPromptValue(std::optional<unsigned int> repetitions,
                                   std::wstring buffer_path) {
  std::optional<Path> path = OptionalFrom(Path::FromString(buffer_path));
  if (path == std::nullopt) return L"";
  struct stat stat_buffer;
  // TODO(blocking): Use FileSystemDriver here!
  if (stat(ToByteString(path->read()).c_str(), &stat_buffer) == -1 ||
      !S_ISDIR(stat_buffer.st_mode)) {
    LOG(INFO) << "Taking dirname for prompt: " << *path;
    std::visit(overload{IgnoreErrors{}, [&](Path dir) { path = dir; }},
               path->Dirname());
  }
  if (*path == Path::LocalDirectory()) {
    return L"";
  }
  if (repetitions.has_value()) {
    if (repetitions.value() == 0) {
      return L"";
    }
    std::visit(overload{IgnoreErrors{},
                        [&](std::list<PathComponent> split) {
                          if (split.size() <= repetitions.value()) return;
                          std::optional<Path> output_path;
                          switch (path->GetRootType()) {
                            case Path::RootType::kAbsolute:
                              output_path = Path::Root();
                              break;
                            case Path::RootType::kRelative:
                              break;
                          }
                          for (size_t i = 0; i < repetitions.value(); i++) {
                            auto part = Path(split.front());
                            split.pop_front();
                            if (output_path.has_value()) {
                              output_path =
                                  Path::Join(output_path.value(), part);
                            } else {
                              output_path = part;
                            }
                          }
                          path = output_path.value();
                        }},
               path->DirectorySplit());
  }
  return path->read() + L"/";
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
                      source_buffers[0].ptr()->Read(buffer_variables::path)),
        .colorize_options_provider =
            std::bind_front(AdjustPath, std::ref(editor)),
        .handler = std::bind_front(OpenFileHandler, std::ref(editor)),
        .cancel_handler =
            [&editor]() {
              VisitPointer(
                  editor.current_buffer(),
                  [](gc::Root<OpenBuffer> buffer) {
                    buffer.ptr()->ResetMode();
                  },
                  [] {});
            },
        .predictor = FilePredictor,
        .source_buffers = source_buffers};
  });
}

}  // namespace afc::editor
