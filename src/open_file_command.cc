#include "src/open_file_command.h"

#include "src/language/wstring.h"

extern "C" {
#include <sys/stat.h>
}

#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/file_predictor.h"
#include "src/futures/delete_notification.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"
#include "src/language/error/view.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/line_prompt_mode.h"
#include "src/tests/tests.h"
#include "src/vm/escape.h"

namespace gc = afc::language::gc;

using afc::futures::DeleteNotification;
using afc::futures::UnwrapVectorFuture;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::NonNull;
using afc::language::OptionalFrom;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::ForEachColumn;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::SortedLineSequence;
using afc::language::text::SortedLineSequenceUniqueLines;
using afc::language::view::SkipErrors;
using afc::vm::EscapedString;
namespace afc::editor {
namespace {

struct PathAndOpenFilePositionSpec {
  Path path;
  open_file_position::Spec spec;
};

futures::ValueOrError<gc::Root<OpenBuffer>> LowLevelOpenFile(
    const OpenFileOptions& options,
    OpenFilesOptions::NotFoundHandler not_found_handler) {
  switch (not_found_handler) {
    case OpenFilesOptions::NotFoundHandler::kCreate:
      return OpenOrCreateFile(options).Transform(
          [](gc::Root<OpenBuffer> buffer) { return Success(buffer); });
    case OpenFilesOptions::NotFoundHandler::kIgnore:
      return OpenFileIfFound(options);
  }
  Error error{LazyString{L"Invalid value for not_found_handler."}};
  LOG(FATAL) << error;
  return futures::Past(error);
}
}  // namespace

futures::Value<std::vector<gc::Root<OpenBuffer>>> OpenFiles(
    OpenFilesOptions options) {
  return GetFilePredictor(FilePredictorOptions{
      .match_behavior = FilePredictorMatchBehavior::kOnlyExactMatch,
      .open_file_position_suffix_mode = options.open_file_position_suffix_mode,
      .directory_filter = options.directory_filter,
      .special_file_filter = options.special_file_filter,
  })(PredictorInput{.editor = options.editor,
                    .input = options.path_pattern,
                    .input_column = {},
                    .source_buffers = {}})
      .Transform([options](PredictorOutput predictor_output)
                     -> futures::Value<
                         std::vector<ValueOrError<gc::Root<OpenBuffer>>>> {
        if (std::vector<PathAndOpenFilePositionSpec> paths =
                predictor_output.contents.read().lines() |
                std::views::transform(
                    [](Line line) -> ValueOrError<PathAndOpenFilePositionSpec> {
                      DECLARE_OR_RETURN(
                          Path path, Path::New(ToLazyString(line.contents())));
                      open_file_position::Spec spec =
                          open_file_position::SpecFromLineMetadata(
                              line.metadata().get());
                      return PathAndOpenFilePositionSpec{.path = path,
                                                         .spec = spec};
                    }) |
                SkipErrors | std::ranges::to<std::vector>();
            !paths.empty())
          return UnwrapVectorFuture(
              paths |
              std::views::transform(
                  [options](PathAndOpenFilePositionSpec input)
                      -> futures::ValueOrError<gc::Root<OpenBuffer>> {
                    return LowLevelOpenFile(
                        OpenFileOptions{
                            .editor_state = options.editor,
                            .path = input.path,
                            .position = input.spec,
                            .insertion_type = options.insertion_type,
                        },
                        options.not_found_handler);
                  }) |
              std::ranges::to<std::vector>());
        if (options.not_found_handler ==
            OpenFilesOptions::NotFoundHandler::kIgnore)
          return std::vector<ValueOrError<gc::Root<OpenBuffer>>>{};
        LOG(INFO) << "No completion found; passing specified path: "
                  << options.path_pattern;
        ValueOrError<Path> path_or_error =
            Path::New(ToLazyString(options.path_pattern));
        if (IsError(path_or_error))
          return std::vector<ValueOrError<gc::Root<OpenBuffer>>>{};
        return LowLevelOpenFile(
                   OpenFileOptions{.editor_state = options.editor,
                                   .path = ValueOrDie(std::move(path_or_error)),
                                   .insertion_type = options.insertion_type},
                   options.not_found_handler)
            .Transform<futures::ErrorHandling::Disable>(
                [](ValueOrError<gc::Root<OpenBuffer>> value) {
                  return std::vector{value};
                });
      })
      .Transform<futures::ErrorHandling::Disable>(
          [](const std::vector<ValueOrError<gc::Root<OpenBuffer>>> output) {
            return output | SkipErrors | std::ranges::to<std::vector>();
          });
}

namespace {
// Returns the buffer to show for context, or nullptr.
futures::Value<std::optional<gc::Root<OpenBuffer>>> StatusContext(
    EditorState& editor, const PredictResults& results, SingleLine line) {
  futures::Value<std::optional<gc::Root<OpenBuffer>>> output =
      futures::Past(std::optional<gc::Root<OpenBuffer>>());
  if (results.predictor_output.found_exact_match) {
    ValueOrError<Path> path_or_error = Path::New(ToLazyString(line));
    if (IsError(path_or_error)) return output;
    output = OpenFileIfFound(
                 OpenFileOptions{
                     .editor_state = editor,
                     .path = ValueOrDie(std::move(path_or_error)),
                     .insertion_type = BuffersList::AddBufferType::kIgnore})
                 .template Transform<futures::ErrorHandling::Disable>(
                     &OptionalFrom<gc::Root<OpenBuffer>>);
  }
  return std::move(output).Transform(
      [results](std::optional<gc::Root<OpenBuffer>> buffer)
          -> std::optional<gc::Root<OpenBuffer>> {
        if (buffer.has_value()) return buffer;
        if (results.predictions_buffer.ptr()->contents().range().empty())
          return std::nullopt;
        LOG(INFO) << "Setting context: "
                  << results.predictions_buffer.ptr()->Read(
                         buffer_variables::name);
        return results.predictions_buffer;
      });
}

ColorizePromptOptions DrawPath(
    SingleLine line, PredictResults results,
    std::optional<gc::Root<OpenBuffer>> context_buffer) {
  ColorizePromptOptions output;
  VisitOptional(
      [&](gc::Root<OpenBuffer> buffer) {
        output.context =
            ColorizePromptOptions::ContextBuffer{.buffer = std::move(buffer)};
      },
      [&] { output.context = ColorizePromptOptions::ContextClear{}; },
      context_buffer);

  ForEachColumn(line, [&](ColumnNumber column, wchar_t c) {
    LineModifierSet modifiers;
    switch (c) {
      case L'/':
      case L'.':
        modifiers.insert(LineModifier::kDim);
        break;
      default:
        if (column.ToDelta() >=
            results.predictor_output.longest_directory_match) {
          if (results.predictor_output.found_exact_match) {
            modifiers.insert(LineModifier::kBold);
          }
          if (results.matches == 0 &&
              column.ToDelta() >= results.predictor_output.longest_prefix) {
            modifiers.insert(LineModifier::kRed);
          } else if (results.matches == 1) {
            modifiers.insert(LineModifier::kGreen);
          } else if (results.common_prefix.has_value() &&
                     line.size() < ColumnNumberDelta(
                                       results.common_prefix.value().size())) {
            modifiers.insert(LineModifier::kYellow);
          }
        }
    }
    output.tokens.push_back(TokenAndModifiers{
        .token = {.value = {}, .begin = column, .end = column.next()},
        .modifiers = std::move(modifiers)});
  });
  return output;
}

futures::Value<ColorizePromptOptions> AdjustPath(
    EditorState& editor, const SingleLine& line,
    NonNull<std::unique_ptr<ProgressChannel>> progress_channel,
    DeleteNotification::Value abort_value) {
  return Predict(GetFilePredictor(FilePredictorOptions{}),
                 PredictorInput{.editor = editor,
                                .input = line,
                                .input_column = ColumnNumber() + line.size(),
                                .source_buffers = editor.active_buffers(),
                                .progress_channel = std::move(progress_channel),
                                .abort_value = std::move(abort_value)})
      .Transform([&editor, line](std::optional<PredictResults> results) {
        if (!results.has_value()) return futures::Past(ColorizePromptOptions{});
        return StatusContext(editor, results.value(), line)
            .Transform(std::bind_front(DrawPath, line, results.value()));
      });
}

Line GetInitialPromptValue(std::optional<unsigned int> repetitions,
                           LazyString buffer_path) {
  std::optional<Path> path = OptionalFrom(Path::New(buffer_path));
  if (path == std::nullopt) return Line{};
  struct stat stat_buffer;
  // TODO(blocking): Use FileSystemDriver here!
  if (stat(path->ToBytes().c_str(), &stat_buffer) == -1 ||
      !S_ISDIR(stat_buffer.st_mode)) {
    LOG(INFO) << "Taking dirname for prompt: " << *path;
    std::visit(overload{IgnoreErrors{}, [&](Path dir) { path = dir; }},
               path->Dirname());
  }
  if (*path == Path::LocalDirectory()) {
    return Line{};
  }
  if (repetitions.has_value()) {
    if (repetitions.value() == 0) {
      return Line{};
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
  return Line{EscapedString::FromString(path->read()).EscapedRepresentation() +
              SINGLE_LINE_CONSTANT(L"/")};
}

const bool get_initial_prompt_value_tests_registration = tests::Register(
    L"GetInitialPromptValue",
    {
        {.name = L"EmptyNoRepetitions",
         .callback =
             [] {
               CHECK_EQ(GetInitialPromptValue({}, LazyString{}).contents(),
                        SingleLine{});
             }},
        {.name = L"EmptyRepetitions",
         .callback =
             [] {
               CHECK_EQ(GetInitialPromptValue(5, LazyString{}).contents(),
                        SingleLine{});
             }},
        {.name = L"NoRepetitionsRelative",
         .callback =
             [] {
               CHECK_EQ(
                   GetInitialPromptValue({}, LazyString{L"foo/bar"}).contents(),
                   SingleLine{LazyString{L"foo/"}});
             }},
        {.name = L"NoRepetitionsAbsolute",
         .callback =
             [] {
               CHECK_EQ(GetInitialPromptValue({}, LazyString{L"/foo/bar"})
                            .contents(),
                        SingleLine{LazyString{L"/foo/"}});
             }},
        {.name = L"ZeroRepetitionsRelative",
         .callback =
             [] {
               CHECK_EQ(
                   GetInitialPromptValue(0, LazyString{L"foo/bar"}).contents(),
                   SingleLine{LazyString{}});
             }},
        {.name = L"ZeroRepetitionsAbsolute",
         .callback =
             [] {
               CHECK_EQ(
                   GetInitialPromptValue(0, LazyString{L"/foo/bar"}).contents(),
                   SingleLine{});
             }},
        {.name = L"LowRepetitionsRelative",
         .callback =
             [] {
               CHECK_EQ(GetInitialPromptValue(2, LazyString{L"a0/b1/c2/d3"})
                            .contents(),
                        SingleLine{LazyString{L"a0/b1/"}});
             }},
        {.name = L"LowRepetitionsAbsolute",
         .callback =
             [] {
               CHECK_EQ(GetInitialPromptValue(2, LazyString{L"/a0/b1/c2/d3"})
                            .contents(),
                        SingleLine{LazyString{L"/a0/b1/"}});
             }},
        {.name = L"BoundaryRepetitionsRelative",
         .callback =
             [] {
               CHECK_EQ(GetInitialPromptValue(3, LazyString{L"a0/b1/c2/d3"})
                            .contents(),
                        SingleLine{LazyString{L"a0/b1/c2/"}});
             }},
        {.name = L"BoundaryRepetitionsAbsolute",
         .callback =
             [] {
               CHECK_EQ(GetInitialPromptValue(3, LazyString{L"/a0/b1/c2/d3"})
                            .contents(),
                        SingleLine{LazyString{L"/a0/b1/c2/"}});
             }},
        {.name = L"HighRepetitionsRelative",
         .callback =
             [] {
               CHECK_EQ(GetInitialPromptValue(40, LazyString{L"a0/b1/c2/d3"})
                            .contents(),
                        SingleLine{LazyString{L"a0/b1/c2/"}});
             }},
        {.name = L"HighRepetitionsAbsolute",
         .callback =
             [] {
               CHECK_EQ(GetInitialPromptValue(40, LazyString{L"/a0/b1/c2/d3"})
                            .contents(),
                        SingleLine{LazyString{L"/a0/b1/c2/"}});
             }},
    });
}  // namespace

gc::Root<Command> NewOpenFileCommand(EditorState& editor) {
  return NewLinePromptCommand(editor, L"loads a file", [&editor]() {
    auto source_buffers = editor.active_buffers();
    return PromptOptions{
        .editor_state = editor,
        .prompt = LineBuilder{SingleLine{LazyString{L"<"}}}.Build(),
        .prompt_contents_type = LazyString{L"path"},
        .history_file = HistoryFileFiles(),
        .initial_value =
            source_buffers.empty()
                ? Line{}
                : GetInitialPromptValue(
                      editor.modifiers().repetitions,
                      source_buffers[0].ptr()->Read(buffer_variables::path)),
        .colorize_options_provider =
            std::bind_front(AdjustPath, std::ref(editor)),
        .handler =
            [&editor](SingleLine value) {
              return OpenFiles(
                         OpenFilesOptions{
                             .editor = editor,
                             .not_found_handler =
                                 OpenFilesOptions::NotFoundHandler::kCreate,
                             .path_pattern = value})
                  .Transform([](auto) { return EmptyValue{}; });
            },
        .cancel_handler =
            [&editor]() {
              VisitPointer(
                  editor.current_buffer(),
                  [](gc::Root<OpenBuffer> buffer) {
                    buffer.ptr()->ResetMode();
                  },
                  [] {});
            },
        .predictor = GetFilePredictor(FilePredictorOptions{}),
        .source_buffers = source_buffers};
  });
}

}  // namespace afc::editor
