#include "src/fragments.h"

#include "src/buffer.h"
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

using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::overload;
using afc::language::VisitOptional;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineSequence;
using afc::vm::EscapedMap;

namespace afc::editor {

namespace {
const vm::Identifier kIdentifierFragment{LazyString{L"fragment"}};

futures::Value<gc::Root<OpenBuffer>> GetFragmentsBuffer(EditorState& editor) {
  return VisitOptional(
      [](gc::Root<OpenBuffer> output) { return futures::Past(output); },
      [&editor] {
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
                  [buffer_root](EmptyValue) { return buffer_root; });
            });
      },
      editor.buffer_registry().Find(FragmentsBuffer{}));
}
}  // namespace

void AddFragment(EditorState& editor, LineSequence fragment) {
  GetFragmentsBuffer(editor).Transform(
      [fragment](gc::Root<OpenBuffer> fragments_buffer) {
        fragments_buffer.ptr()->AppendLine(vm::EscapedMap{
            std::multimap<vm::Identifier, LazyString>{
                {kIdentifierFragment,
                 fragment.ToLazyString()}}}.Serialize());
        return futures::Past(EmptyValue{});
      });
}

futures::Value<language::text::LineSequence> FindFragment(EditorState& editor,
                                                          FindFragmentQuery) {
  return GetFragmentsBuffer(editor).Transform(
      [](gc::Root<OpenBuffer> fragments_buffer) {
        return std::visit(
            overload{// TODO(trivial, 2024-09-10): Don't ignore the error.
                     [](Error) { return LineSequence{}; },
                     [](EscapedMap parsed_map) {
                       auto it = parsed_map.read().find(kIdentifierFragment);
                       if (it == parsed_map.read().end()) return LineSequence{};
                       return LineSequence::BreakLines(it->second);
                     }},
            EscapedMap::Parse(fragments_buffer.ptr()
                                  ->contents()
                                  .snapshot()
                                  .back()
                                  .contents()
                                  .read()));
      });
}
}  // namespace afc::editor