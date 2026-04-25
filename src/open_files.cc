#include "src/open_files.h"

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
  return MakeUnexpected(error);
}
}  // namespace

futures::Value<std::vector<gc::Root<OpenBuffer>>> OpenFiles(
    OpenFilesOptions options) {
  return GetFilePredictor(FilePredictorOptions{
      .match_type = FilePredictorMatchType::Exact,
      .match_limit = options.match_limit,
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
        return LowLevelOpenFile(
                   OpenFileOptions{
                       .editor_state = options.editor,
                       .path = OptionalFrom(std::move(path_or_error)),
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
}  // namespace afc::editor
