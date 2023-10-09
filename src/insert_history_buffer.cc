#include "src/insert_history_buffer.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/buffers_list.h"
#include "src/editor.h"
#include "src/insert_history.h"
#include "src/language/text/line_sequence.h"

namespace afc::editor {
using language::NonNull;
using language::PossibleError;
using language::Success;
using language::text::LineColumn;
using language::text::LineSequence;
using language::text::MutableLineSequence;

namespace {
futures::Value<PossibleError> InsertHistoryBufferContents(OpenBuffer& output) {
  output.ClearContents(MutableLineSequence::ObserverBehavior::kHide);
  for (const LineSequence& contents : output.editor().insert_history().get()) {
    LineColumn position = output.contents().range().end();
    output.InsertInPosition(contents, position, {});
    output.AppendEmptyLine();
  };
  return futures::Past(Success());
}
}  // namespace

void ShowInsertHistoryBuffer(EditorState& editor) {
  const BufferName name(L"- Insert History");

  auto buffer_root =
      OpenBuffer::New({.editor = editor,
                       .name = name,
                       .generate_contents = &InsertHistoryBufferContents});
  OpenBuffer& buffer = buffer_root.ptr().value();

  buffer.Set(buffer_variables::tree_parser, L"md");
  buffer.Set(buffer_variables::wrap_from_content, true);
  buffer.Set(buffer_variables::allow_dirty_delete, true);

  buffer.Reload();

  editor.buffers()->insert_or_assign(name, buffer_root.ptr().ToRoot());
  editor.AddBuffer(buffer_root, BuffersList::AddBufferType::kVisit);
  editor.ResetRepetitions();
}
}  // namespace afc::editor
