#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/time.h"
#include "src/language/error/value_or_error.h"
#include "src/language/text/line_builder.h"
#include "src/open_files.h"
#include "src/run_command_handler.h"
#include "src/url.h"
#include "src/vm/escape.h"

namespace gc = afc::language::gc;

using afc::futures::IterationControlCommand;
using afc::infrastructure::AddSeconds;
using afc::infrastructure::Now;
using afc::infrastructure::Path;
using afc::language::Error;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::LineBuilder;

namespace afc::editor {

futures::ValueOrError<std::optional<gc::Root<OpenBuffer>>> HandleFileURL(
    EditorState& editor, const URL& url) {
  DECLARE_OR_RETURN(Path path, url.GetLocalFilePath());
  // Converting it to SingleLine (rather than LazyString) is suboptimal: it
  // would be good, in theory, to support paths that have a \n in them. However,
  // OpenFilesOptions::path_pattern is a SingleLine. It probably doesn't matter
  // in practice since the URLs come from lines in the buffer.
  DECLARE_OR_RETURN(SingleLine path_str,
                    SingleLine::New(ToLazyString(std::move(path))));
  TRACK_OPERATION(OpenBuffer_OpenBufferForCurrentPosition);
  VLOG(4) << "Calling open file: " << path_str;
  return OpenFiles(
             OpenFilesOptions{
                 .editor = editor,
                 .match_limit = 1,
                 .not_found_handler = OpenFilesOptions::NotFoundHandler::Ignore,
                 .path_pattern = std::move(path_str),
                 .open_file_position_suffix_mode =
                     open_file_position::SuffixMode::Allow,
                 .insertion_type = BuffersList::AddBufferType::Ignore,
                 .special_file_filter = FilePredictorOptions::Filter::Exclude})
      .Transform(
          [](std::vector<gc::Root<OpenBuffer>> buffers)
              -> futures::ValueOrError<std::optional<gc::Root<OpenBuffer>>> {
            if (buffers.empty()) return Error{L"No file found."};
            return buffers[0];
          });
}

futures::ValueOrError<std::optional<gc::Root<OpenBuffer>>> HandleVmURL(
    EditorState& editor, ExecutionContext& execution_context, const URL& url) {
  VLOG(6) << "Checking VM URL: " << url;
  SingleLine code = url.StripScheme();
  editor.work_queue()->DeleteLater(
      AddSeconds(Now(), 1.0),
      editor.status().SetExpiringInformationText(
          LineBuilder{SINGLE_LINE_CONSTANT(L"Vm: ") + code}.Build()));
  DECLARE_OR_RETURN(
      gc::Root<ExecutionContext::CompilationResult> compilation_result,
      execution_context.CompileString(ToLazyString(code)));
  VLOG(7) << "Evaluating VM URL: " << code;
  return compilation_result->evaluate().Transform(
      [](gc::Root<vm::Value>)
          -> ValueOrError<std::optional<gc::Root<OpenBuffer>>> {
        return std::nullopt;
      });
}

futures::ValueOrError<std::optional<gc::Root<OpenBuffer>>> HandleURL(
    EditorState& editor, ExecutionContext& execution_context,
    RemoteURLBehavior remote_url_behavior, VmURLBehavior vm_url_behavior,
    const URL& url) {
  VLOG(5) << "Checking URL: " << url;
  const URL::Scheme scheme = url.scheme().value_or(URL::Scheme::File);
  switch (scheme) {
    case URL::Scheme::Http:
    case URL::Scheme::Https:
      switch (remote_url_behavior) {
        case RemoteURLBehavior::Ignore:
          return std::nullopt;
        case RemoteURLBehavior::LaunchBrowser:
          editor.work_queue()->DeleteLater(
              AddSeconds(Now(), 1.0),
              editor.status().SetExpiringInformationText(
                  LineBuilder{SINGLE_LINE_CONSTANT(L"Open: ") + url.read()}
                      .Build()));
          ForkCommand(editor,
                      RunCommandOptions{
                          .command = LazyString{L"xdg-open "} +
                                     vm::EscapedString(ToLazyString(url))
                                         .ShellEscapedRepresentation(),
                          .insertion_type = BuffersList::AddBufferType::Ignore,
                      });
      }
      return std::nullopt;
    case URL::Scheme::File:
      return HandleFileURL(editor, url);
    case URL::Scheme::Vm:
      switch (vm_url_behavior) {
        case VmURLBehavior::Ignore:
          return std::nullopt;
        case VmURLBehavior::Execute:
          return HandleVmURL(editor, execution_context, url);
      }
  }
  LOG(FATAL) << "Invalid scheme.";
  std::unreachable();
}
}  // namespace afc::editor
