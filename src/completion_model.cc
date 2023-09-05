#include "src/completion_model.h"

#include "src/buffer_variables.h"
#include "src/file_link_mode.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/tests/tests.h"

namespace afc::editor::completion {
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::lazy_string::Substring;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;

using ::operator<<;

namespace gc = afc::language::gc;

namespace {
void PrepareBuffer(OpenBuffer& buffer) {
  buffer.Set(buffer_variables::allow_dirty_delete, true);
  buffer.SortAllContentsIgnoringCase();
  LineNumber empty_lines_start;
  while (empty_lines_start < buffer.contents().EndLine() &&
         buffer.contents().at(empty_lines_start)->contents()->size().IsZero())
    ++empty_lines_start;
  if (!empty_lines_start.IsZero()) {
    LOG(INFO) << "Deleting empty lines: " << empty_lines_start << " to "
              << buffer.contents().EndLine();
    buffer.EraseLines(LineNumber(), empty_lines_start);
  }
}

gc::Root<OpenBuffer> CompletionModelForTests() {
  gc::Root<OpenBuffer> buffer = NewBufferForTests();
  buffer.ptr()->AppendToLastLine(NewLazyString(L"bb baby"));
  buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L"f fox"));
  return buffer;
}

const bool prepare_buffer_tests_registration = tests::Register(
    L"CompletionModel::PrepareBuffer",
    {{.name = L"EmptyBuffer",
      .callback =
          [] {
            gc::Root<OpenBuffer> buffer = NewBufferForTests();
            PrepareBuffer(buffer.ptr().value());
            CHECK(buffer.ptr()->contents().ToString() == L"");
          }},
     {.name = L"UnsortedBuffer", .callback = [] {
        gc::Root<OpenBuffer> buffer = NewBufferForTests();
        buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L"f fox"));
        buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L""));
        buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L""));
        buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L"b baby"));
        buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L""));
        CHECK(buffer.ptr()->contents().ToString() == L"\nf fox\n\n\nb baby\n");
        PrepareBuffer(buffer.ptr().value());
        LOG(INFO) << "After sort: [" << buffer.ptr()->contents().ToString()
                  << "]";
        CHECK(buffer.ptr()->contents().ToString() == L"b baby\nf fox");
      }}});

}  // namespace

futures::Value<CompletionModel> LoadModel(EditorState& editor, Path path) {
  return OpenOrCreateFile(
             OpenFileOptions{
                 .editor_state = editor,
                 .path = Path::Join(editor.edge_path().front(), path),
                 .insertion_type = BuffersList::AddBufferType::kIgnore,
                 .use_search_paths = false})
      .Transform([](gc::Root<OpenBuffer> buffer) {
        return buffer.ptr()->WaitForEndOfFile().Transform([buffer](EmptyValue) {
          PrepareBuffer(buffer.ptr().value());
          return buffer;
        });
      });
}

namespace {
std::optional<Text> FindCompletionInModel(
    const gc::Root<OpenBuffer>& model, const CompressedText& compressed_text) {
  const BufferContents& contents = model.ptr()->contents();

  VLOG(3) << "Starting completion with model with size: " << contents.size()
          << " token: " << compressed_text->ToString();

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
  if (compressed_text.value() != model_compressed_text.value()) {
    VLOG(5) << "No match: [" << compressed_text->ToString() << "] != ["
            << model_compressed_text->ToString() << "]";
    return std::nullopt;
  }
  Text output = Text(Substring(line_contents, ColumnNumber(split + 1)));
  VLOG(2) << "Found compression: " << compressed_text->ToString() << " -> "
          << output->ToString();
  return output;
}

futures::Value<std::optional<Text>> FindCompletionWithIndex(
    std::shared_ptr<std::vector<futures::Value<gc::Root<OpenBuffer>>>> models,
    CompressedText compressed_text, size_t index) {
  if (index == models->size()) return futures::Past(std::optional<Text>());
  futures::Value<gc::Root<OpenBuffer>> current_future =
      std::move(models->at(index));
  return std::move(current_future)
      .Transform([models = std::move(models), compressed_text,
                  index](gc::Root<OpenBuffer> model) mutable {
        if (std::optional<Text> result =
                FindCompletionInModel(model, compressed_text);
            result.has_value())
          return futures::Past(result);
        return FindCompletionWithIndex(std::move(models), compressed_text,
                                       index + 1);
      });
}

const bool find_completion_tests_registration = tests::Register(
    L"CompletionModel::FindCompletion",
    {
        {.name = L"NoModelAvailable",
         .callback =
             [] {
               std::optional<Text> output =
                   FindCompletion({}, CompressedText(NewLazyString(L"foo")))
                       .Get()
                       .value();
               CHECK(output == std::nullopt);
             }},
        {.name = L"EmptyModel",
         .callback =
             [] {
               std::vector<futures::Value<CompletionModel>> models;
               models.push_back(futures::Past(NewBufferForTests()));
               std::optional<Text> output =
                   FindCompletion(std::move(models),
                                  CompressedText(NewLazyString(L"foo")))
                       .Get()
                       .value();
               CHECK(output == std::nullopt);
             }},
        {.name = L"NoMatch",
         .callback =
             [] {
               std::vector<futures::Value<CompletionModel>> models;
               models.push_back(futures::Past(CompletionModelForTests()));
               std::optional<Text> output =
                   FindCompletion(std::move(models),
                                  CompressedText(NewLazyString(L"foo")))
                       .Get()
                       .value();
               CHECK(output == std::nullopt);
             }},
        {.name = L"Match",
         .callback =
             [] {
               std::vector<futures::Value<CompletionModel>> models;
               models.push_back(futures::Past(CompletionModelForTests()));
               std::optional<Text> output =
                   FindCompletion(std::move(models),
                                  CompressedText(NewLazyString(L"f")))
                       .Get()
                       .value();
               CHECK(output->value() == NewLazyString(L"fox").value());
             }},
    });
}  // namespace

futures::Value<std::optional<Text>> FindCompletion(
    std::vector<futures::Value<CompletionModel>> models,
    CompressedText compressed_text) {
  return FindCompletionWithIndex(
      std::make_shared<std::vector<futures::Value<CompletionModel>>>(
          std::move(models)),
      std::move(compressed_text), 0);
}

}  // namespace afc::editor::completion
