#include "src/insert_history_buffer.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/buffers_list.h"
#include "src/editor.h"
#include "src/insert_history.h"
#include "src/language/text/line_sequence.h"
#include "src/parsers/markdown.h"

namespace gc = afc::language::gc;

using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor {

namespace {
futures::Value<PossibleError> InsertHistoryBufferContents(OpenBuffer& output) {
  for (const LineSequence& contents : output.editor().insert_history().get()) {
    LineColumn position = output.contents().range().end();
    output.InsertInPosition(contents, position, {});
    output.AppendEmptyLine();
  };
  return futures::Past(Success());
}
}  // namespace

void ShowInsertHistoryBuffer(EditorState& editor) {
  const BufferName name{LazyString{L"- Insert History"}};

  gc::Root<OpenBuffer> buffer_root =
      OpenBuffer::New({.editor = editor,
                       .name = name,
                       .generate_contents = &InsertHistoryBufferContents});
  OpenBuffer& buffer = buffer_root.ptr().value();

  buffer.Set(buffer_variables::tree_parser,
             ToLazyString(ParserId::Markdown().read()));
  buffer.Set(buffer_variables::wrap_from_content, true);
  buffer.Set(buffer_variables::allow_dirty_delete, true);

  buffer.Reload();

  editor.AddBuffer(std::move(buffer_root), BuffersList::AddBufferType::kVisit);
  editor.ResetRepetitions();
}
}  // namespace afc::editor
