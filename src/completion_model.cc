#include "src/completion_model.h"

#include "src/buffer_variables.h"
#include "src/file_link_mode.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/tests/tests.h"

namespace afc::editor::completion {
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language ::overload;
using afc::language::ValueOrError;
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

using CompletionModel = language::gc::Root<OpenBuffer>;

namespace {
struct ParsedLine {
  CompressedText compressed_text;
  Text text;
};

ValueOrError<ParsedLine> Parse(NonNull<std::shared_ptr<LazyString>> line) {
  // TODO(easy, 2023-09-07): Avoid call to ToString.
  size_t split = line->ToString().find_first_of(L" ");
  if (split == std::wstring::npos) return Error(L"No space found.");
  return ParsedLine{.compressed_text = CompressedText(Substring(
                        line, ColumnNumber(), ColumnNumberDelta(split))),
                    .text = Text(Substring(line, ColumnNumber(split + 1)))};
}

void PrepareBuffer(OpenBuffer& buffer) {
  LOG(INFO) << "Completion Model preparing buffer: " << buffer.name();
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
  return std::visit(
      overload{[&](const ParsedLine& parsed_line) -> std::optional<Text> {
                 if (compressed_text.value() !=
                     parsed_line.compressed_text.value()) {
                   VLOG(5) << "No match: ["
                           << parsed_line.compressed_text->ToString()
                           << "] != ["
                           << parsed_line.compressed_text->ToString() << "]";
                   return std::nullopt;
                 }

                 VLOG(2) << "Found compression: "
                         << parsed_line.compressed_text->ToString() << " -> "
                         << parsed_line.text->ToString();
                 return parsed_line.text;
               },
               [](Error) { return std::optional<Text>(); }},
      Parse(contents.at(line)->contents()));
}

const bool find_completion_tests_registration = tests::Register(
    L"completion::FindCompletionInModel",
    {
        {.name = L"EmptyModel",
         .callback =
             [] {
               CHECK(FindCompletionInModel(NewBufferForTests(),
                                           CompressedText(NewLazyString(
                                               L"foo"))) == std::nullopt);
             }},
        {.name = L"NoMatch",
         .callback =
             [] {
               CHECK(FindCompletionInModel(CompletionModelForTests(),
                                           CompressedText(NewLazyString(
                                               L"foo"))) == std::nullopt);
             }},
        {.name = L"Match",
         .callback =
             [] {
               CHECK(FindCompletionInModel(CompletionModelForTests(),
                                           CompressedText(NewLazyString(L"f")))
                         ->value() == NewLazyString(L"fox").value());
             }},
    });
}  // namespace

ModelSupplier::ModelSupplier(EditorState& editor) : editor_(editor) {}

futures::Value<ModelSupplier::QueryOutput> ModelSupplier::FindCompletion(
    std::vector<Path> models, CompressedText compressed_text) {
  return FindCompletionWithIndex(
      editor_, data_, std::make_shared<std::vector<Path>>(std::move(models)),
      std::move(compressed_text), 0);
}

/* static */
futures::Value<ModelSupplier::QueryOutput>
ModelSupplier::FindCompletionWithIndex(
    EditorState& editor,
    NonNull<std::shared_ptr<concurrent::Protected<Data>>> data,
    std::shared_ptr<std::vector<Path>> models_list,
    CompressedText compressed_text, size_t index) {
  if (index == models_list->size())
    return futures::Past(
        data->lock([&](const Data& locked_data) -> QueryOutput {
          Text text = compressed_text;
          if (auto text_it = locked_data.reverse_table.find(text->ToString());
              text_it != locked_data.reverse_table.end()) {
            for (const Path& path : *models_list)
              if (auto path_it = text_it->second.find(path);
                  path_it != text_it->second.end())
                return Suggestion{.compressed_text = path_it->second};
          }
          return NothingFound{};
        }));

  futures::ListenableValue<gc::Root<OpenBuffer>> current_future =
      data->lock([&](Data& locked_data) {
        Path path = models_list->at(index);
        if (auto it = locked_data.models.find(path);
            it != locked_data.models.end())
          return it->second;
        auto output =
            locked_data.models
                .insert(
                    {path, futures::ListenableValue(LoadModel(editor, path))})
                .first->second;
        output.AddListener([data, path](const gc::Root<OpenBuffer>& buffer) {
          data->lock([&](Data& data_locked) {
            UpdateReverseTable(data_locked, path, buffer.ptr()->contents());
          });
        });
        return output;
      });

  return std::move(current_future)
      .ToFuture()
      .Transform([&editor, data = std::move(data),
                  models_list = std::move(models_list), compressed_text,
                  index](gc::Root<OpenBuffer> model) mutable {
        if (std::optional<Text> result =
                FindCompletionInModel(model, compressed_text);
            result.has_value())
          return futures::Past(QueryOutput(*result));
        return FindCompletionWithIndex(editor, std::move(data),
                                       std::move(models_list), compressed_text,
                                       index + 1);
      });
}
/* static */ void ModelSupplier::UpdateReverseTable(
    Data& data, const Path& path, const BufferContents& contents) {
  contents.ForEach([&](const Line& line) {
    std::visit(overload{[&path, &data](const ParsedLine& line) {
                          data.reverse_table[line.text->ToString()].insert(
                              {path, line.compressed_text});
                        },
                        IgnoreErrors{}},
               Parse(line.contents()));
  });
}

}  // namespace afc::editor::completion
