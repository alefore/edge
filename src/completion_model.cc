#include "src/completion_model.h"

#include "src/file_link_mode.h"

namespace afc::editor::completion {
using afc::futures::ListenableValue;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;

namespace gc = afc::language::gc;

futures::ListenableValue<CompletionModel> LoadModel(EditorState& editor,
                                                    Path path) {
  return ListenableValue<CompletionModel>(
      OpenOrCreateFile(
          OpenFileOptions{.editor_state = editor,
                          .path = Path::Join(editor.edge_path().front(), path),
                          .insertion_type = BuffersList::AddBufferType::kIgnore,
                          .use_search_paths = false})
          .Transform([](gc::Root<OpenBuffer> buffer) {
            return buffer.ptr()->WaitForEndOfFile().Transform(
                [buffer](EmptyValue) {
                  buffer.ptr()->SortAllContentsIgnoringCase();
                  return buffer;
                });
          }));
}

futures::Value<std::optional<Text>> FindCompletion(
    futures::ListenableValue<gc::Root<OpenBuffer>> model,
    CompressedText compressed_text);
}  // namespace afc::editor::completion
