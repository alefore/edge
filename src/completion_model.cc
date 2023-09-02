#include "src/completion_model.h"

#include "src/file_link_mode.h"

namespace afc::editor::completion {
using afc::futures::ListenableValue;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::Substring;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;

using ::operator<<;

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

namespace {
std::optional<Text> FindCompletion(const gc::Root<OpenBuffer>& model,
                                   const CompressedText& compressed_text) {
  const BufferContents& contents = model.ptr()->contents();

  // TODO(trivial, 2023-09-01): Handle the case where the last line is
  // empty.
  LineNumber line = contents.upper_bound(
      MakeNonNullShared<const Line>(LineBuilder(compressed_text).Build()),
      [](const NonNull<std::shared_ptr<const Line>>& a,
         const NonNull<std::shared_ptr<const Line>>& b) {
        return a->ToString() < b->ToString();
      });

  if (line > contents.EndLine()) return std::nullopt;
  NonNull<std::shared_ptr<LazyString>> line_contents =
      contents.at(line)->contents();
  // TODO(easy, 2023-09-01): Avoid calls to ToString, ugh.
  VLOG(5) << "Check: " << compressed_text->ToString()
          << " against: " << line_contents->ToString();
  size_t split = line_contents->ToString().find_first_of(L" ");
  if (split == std::wstring::npos) return std::nullopt;
  CompressedText model_compressed_text = CompressedText(
      Substring(line_contents, ColumnNumber(), ColumnNumberDelta(split)));
  if (compressed_text != model_compressed_text) return std::nullopt;
  return Text(Substring(line_contents, ColumnNumber(split + 1)));
}

futures::Value<std::optional<Text>> FindCompletion(
    std::vector<ListenableValue<gc::Root<OpenBuffer>>> models,
    CompressedText compressed_text, size_t index) {
  if (index == models.size()) return futures::Past(std::optional<Text>());
  futures::Value<gc::Root<OpenBuffer>> current_future =
      models[index].ToFuture();
  return std::move(current_future)
      .Transform([models = std::move(models), compressed_text,
                  index](gc::Root<OpenBuffer> model) mutable {
        if (std::optional<Text> result = FindCompletion(model, compressed_text);
            result.has_value())
          return futures::Past(result);
        return FindCompletion(std::move(models), compressed_text, index + 1);
      });
}
}  // namespace

futures::Value<std::optional<Text>> FindCompletion(
    std::vector<futures::ListenableValue<CompletionModel>> models,
    CompressedText compressed_text) {
  return FindCompletion(std::move(models), std::move(compressed_text), 0);
}

}  // namespace afc::editor::completion