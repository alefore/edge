#include "src/fragments.h"

#include "src/buffer.h"
#include "src/buffer_filter.h"
#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command_argument_mode.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/escape.h"

namespace gc = afc::language::gc;

using afc::futures::DeleteNotification;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;
using afc::language::text::LineSequence;
using afc::vm::EscapedMap;
using afc::vm::EscapedString;

namespace afc::editor {

namespace {
futures::Value<gc::Root<OpenBuffer>> GetFragmentsBuffer(EditorState& editor) {
  return VisitOptional(
      [](gc::Root<OpenBuffer> output) {
        VLOG(6) << "Reusing previous fragments buffer.";
        return futures::Past(output);
      },
      [&editor] {
        VLOG(5) << "Creating (loading) fragments buffer.";
        return OpenOrCreateFile(
                   {.editor_state = editor,
                    .name = FragmentsBuffer{},
                    .path = editor.edge_path().empty()
                                ? std::nullopt
                                : std::make_optional(Path::Join(
                                      editor.edge_path().front(),
                                      PathComponent::FromString(L"fragments"))),
                    .insertion_type = BuffersList::AddBufferType::kIgnore})
            .Transform([&editor](gc::Root<OpenBuffer> buffer_root) {
              VLOG(6) << "Preparing fragments buffer (will wait for EOF).";
              OpenBuffer& buffer = buffer_root.ptr().value();
              buffer.Set(buffer_variables::save_on_close, true);
              buffer.Set(buffer_variables::trigger_reload_on_buffer_write,
                         false);
              buffer.Set(buffer_variables::show_in_buffers_list, false);
              buffer.Set(buffer_variables::atomic_lines, true);
              buffer.Set(buffer_variables::close_after_idle_seconds, 20.0);
              buffer.Set(buffer_variables::vm_lines_evaluation, false);
              if (!editor.has_current_buffer()) {
                // Seems lame, but what can we do?
                editor.set_current_buffer(buffer_root,
                                          CommandArgumentModeApplyMode::kFinal);
              }
              return buffer_root.ptr()->WaitForEndOfFile().Transform(
                  [buffer_root](EmptyValue) {
                    VLOG(6) << "Fragments buffer: EOF received.";
                    return buffer_root;
                  });
            });
      },
      editor.buffer_registry().Find(FragmentsBuffer{}));
}
}  // namespace

void AddFragment(EditorState& editor, LineSequence fragment) {
  GetFragmentsBuffer(editor).Transform(
      [fragment](gc::Root<OpenBuffer> fragments_buffer) {
        fragments_buffer.ptr()->AppendLine(vm::EscapedMap{
            std::multimap<vm::Identifier, EscapedString>{
                {HistoryIdentifierValue(),
                 EscapedString{fragment}}}}.Serialize());
        return futures::Past(EmptyValue{});
      });
}

futures::Value<std::vector<FilterSortBufferOutput::Match>> FindFragment(
    EditorState& editor, FindFragmentQuery query) {
  return GetFragmentsBuffer(editor).Transform(
      [&editor, filter = query.filter](gc::Root<OpenBuffer> fragments_buffer) {
        const LineSequence history =
            fragments_buffer.ptr()->contents().snapshot();
        if (filter.empty()) {
          return futures::Past(std::visit(
              overload{[](Error) {
                         return std::vector<FilterSortBufferOutput::Match>{};
                       },
                       [](EscapedMap parsed_map) {
                         if (auto it = parsed_map.read().find(
                                 HistoryIdentifierValue());
                             it != parsed_map.read().end())
                           return std::vector<FilterSortBufferOutput::Match>{
                               FilterSortBufferOutput::Match{
                                   .preview =
                                       Line{it->second.EscapedRepresentation()},
                                   .data = LineSequence::BreakLines(
                                       it->second.OriginalString())}};
                         return std::vector<FilterSortBufferOutput::Match>{};
                       }},
              EscapedMap::Parse(history.back().contents().read())));
        }
        return editor.thread_pool()
            .Run(std::bind_front(FilterSortBuffer,
                                 FilterSortBufferInput{
                                     .abort_value = DeleteNotification::Never(),
                                     .filter = filter,
                                     .history = history,
                                     .current_features = {}}))
            .Transform(
                [](FilterSortBufferOutput output) { return output.matches; });
      });
}
}  // namespace afc::editor
