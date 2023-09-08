#include "src/completion_model.h"

#include "src/buffer_variables.h"
#include "src/file_link_mode.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/tests/tests.h"

namespace afc::editor {
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language ::overload;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language ::lazy_string::LowerCase;
using afc::language::lazy_string::NewLazyString;
using afc::language::lazy_string::Substring;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;

using ::operator<<;

namespace gc = afc::language::gc;

namespace {
struct ParsedLine {
  CompletionModelManager::CompressedText compressed_text;
  CompletionModelManager::Text text;
};

ValueOrError<ParsedLine> Parse(NonNull<std::shared_ptr<LazyString>> line) {
  // TODO(easy, 2023-09-07): Avoid call to ToString.
  size_t split = line->ToString().find_first_of(L" ");
  if (split == std::wstring::npos) return Error(L"No space found.");
  return ParsedLine{
      .compressed_text = CompletionModelManager::CompressedText(
          Substring(line, ColumnNumber(), ColumnNumberDelta(split))),
      .text = CompletionModelManager::Text(
          Substring(line, ColumnNumber(split + 1)))};
}

BufferContents PrepareBuffer(OpenBuffer& buffer) {
  BufferContents contents = buffer.contents();
  contents.sort(LineNumber(), contents.EndLine() + LineNumberDelta(1),
                [](const NonNull<std::shared_ptr<const Line>>& a,
                   const NonNull<std::shared_ptr<const Line>>& b) {
                  return LowerCase(a->contents()).value() <
                         LowerCase(b->contents()).value();
                });
  LineNumber empty_lines_start;
  while (empty_lines_start < contents.EndLine() &&
         contents.at(empty_lines_start)->contents()->size().IsZero())
    ++empty_lines_start;
  if (!empty_lines_start.IsZero()) {
    LOG(INFO) << "Deleting empty lines: " << empty_lines_start << " to "
              << contents.EndLine();
    contents.EraseLines(LineNumber(), empty_lines_start,
                        BufferContents::CursorsBehavior ::kUnmodified);
  }
  return contents;
}

BufferContents CompletionModelForTests() {
  gc::Root<OpenBuffer> buffer = NewBufferForTests();
  buffer.ptr()->AppendToLastLine(NewLazyString(L"bb baby"));
  buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L"f fox"));
  buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L"i i"));
  return PrepareBuffer(buffer.ptr().value());
}

const bool prepare_buffer_tests_registration = tests::Register(
    L"CompletionModelManager::PrepareBuffer",
    {{.name = L"EmptyBuffer",
      .callback =
          [] {
            gc::Root<OpenBuffer> buffer = NewBufferForTests();
            CHECK(PrepareBuffer(buffer.ptr().value()).ToString() == L"");
          }},
     {.name = L"UnsortedBuffer", .callback = [] {
        gc::Root<OpenBuffer> buffer = NewBufferForTests();
        buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L"f fox"));
        buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L""));
        buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L""));
        buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L"b baby"));
        buffer.ptr()->AppendRawLine(MakeNonNullShared<Line>(L""));
        CHECK(buffer.ptr()->contents().ToString() == L"\nf fox\n\n\nb baby\n");
        CHECK(PrepareBuffer(buffer.ptr().value()).ToString() ==
              L"b baby\nf fox");
      }}});

std::optional<CompletionModelManager::Text> FindCompletionInModel(
    const BufferContents& contents,
    const CompletionModelManager::CompressedText& compressed_text) {
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
      overload{
          [&](const ParsedLine& parsed_line)
              -> std::optional<CompletionModelManager::Text> {
            if (compressed_text.value() !=
                parsed_line.compressed_text.value()) {
              VLOG(5) << "No match: ["
                      << parsed_line.compressed_text->ToString() << "] != ["
                      << parsed_line.compressed_text->ToString() << "]";
              return std::nullopt;
            }

            if (compressed_text.value() == parsed_line.text.value()) {
              VLOG(4) << "Found a match, but the line has compressed text "
                         "identical to parsed text, so we'll skip it.";
              return std::nullopt;
            }

            VLOG(2) << "Found compression: "
                    << parsed_line.compressed_text->ToString() << " -> "
                    << parsed_line.text->ToString();
            return parsed_line.text;
          },
          [](Error) { return std::optional<CompletionModelManager::Text>(); }},
      Parse(contents.at(line)->contents()));
}

const bool find_completion_tests_registration = tests::Register(
    L"completion::FindCompletionInModel",
    {{.name = L"EmptyModel",
      .callback =
          [] {
            CHECK(FindCompletionInModel(BufferContents(),
                                        CompletionModelManager::CompressedText(
                                            NewLazyString(L"foo"))) ==
                  std::nullopt);
          }},
     {.name = L"NoMatch",
      .callback =
          [] {
            CHECK(FindCompletionInModel(CompletionModelForTests(),
                                        CompletionModelManager::CompressedText(
                                            NewLazyString(L"foo"))) ==
                  std::nullopt);
          }},
     {.name = L"Match",
      .callback =
          [] {
            CHECK(FindCompletionInModel(CompletionModelForTests(),
                                        CompletionModelManager::CompressedText(
                                            NewLazyString(L"f")))
                      ->value() == NewLazyString(L"fox").value());
          }},
     {.name = L"IdenticalMatch", .callback = [] {
        CHECK(FindCompletionInModel(CompletionModelForTests(),
                                    CompletionModelManager::CompressedText(
                                        NewLazyString(L"i"))) == std::nullopt);
      }}});
}  // namespace

CompletionModelManager::CompletionModelManager(BufferLoader buffer_loader)
    : buffer_loader_(std::move(buffer_loader)) {}

futures::Value<CompletionModelManager::QueryOutput>
CompletionModelManager::Query(
    std::vector<Path> models,
    CompletionModelManager::CompressedText compressed_text) {
  return FindCompletionWithIndex(
      buffer_loader_, data_,
      std::make_shared<std::vector<Path>>(std::move(models)),
      std::move(compressed_text), 0);
}

/* static */
futures::Value<CompletionModelManager::QueryOutput>
CompletionModelManager::FindCompletionWithIndex(
    BufferLoader buffer_loader,
    NonNull<std::shared_ptr<concurrent::Protected<Data>>> data,
    std::shared_ptr<std::vector<Path>> models_list,
    CompletionModelManager::CompressedText compressed_text, size_t index) {
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

  futures::ListenableValue<BufferContents> current_future =
      data->lock([&](Data& locked_data) {
        Path path = models_list->at(index);
        if (auto it = locked_data.models.find(path);
            it != locked_data.models.end())
          return it->second;
        auto output =
            locked_data.models
                .insert(
                    {path,
                     futures::ListenableValue(buffer_loader(path).Transform(
                         [](gc::Root<OpenBuffer> buffer) {
                           return buffer.ptr()->WaitForEndOfFile().Transform(
                               [buffer](EmptyValue) {
                                 LOG(INFO)
                                     << "Completion Model preparing buffer: "
                                     << buffer.ptr()->name();
                                 return PrepareBuffer(buffer.ptr().value());
                               });
                         }))})
                .first->second;
        // TODO(P2, 2023-09-08, RaceCondition): There's a race here where output
        // may get a value after this check but before the execution of
        // AddListener below. If that happens, we'll deadlock. Figure out a
        // better solution.
        if (output.has_value()) {
          UpdateReverseTable(locked_data, path, output.get_copy().value());
        } else {
          LOG(INFO) << "Adding listener to update reverse table.";
          output.AddListener([data, path](const BufferContents& contents) {
            LOG(INFO) << "Updating reverse table.";
            data->lock([&](Data& data_locked) {
              UpdateReverseTable(data_locked, path, contents);
            });
          });
        }
        return output;
      });

  return std::move(current_future)
      .ToFuture()
      .Transform([buffer_loader = std::move(buffer_loader),
                  data = std::move(data), models_list = std::move(models_list),
                  compressed_text, index](BufferContents contents) mutable {
        if (std::optional<Text> result =
                FindCompletionInModel(contents, compressed_text);
            result.has_value())
          return futures::Past(QueryOutput(*result));
        return FindCompletionWithIndex(buffer_loader, std::move(data),
                                       std::move(models_list), compressed_text,
                                       index + 1);
      });
}
/* static */ void CompletionModelManager::UpdateReverseTable(
    Data& data, const Path& path, const BufferContents& contents) {
  contents.ForEach([&](const Line& line) {
    std::visit(overload{[&path, &data](const ParsedLine& line) {
                          if (line.text.value() != line.compressed_text.value())
                            data.reverse_table[line.text->ToString()].insert(
                                {path, line.compressed_text});
                        },
                        IgnoreErrors{}},
               Parse(line.contents()));
  });
}

namespace {
const bool completion_model_manager_tests_registration =
    tests::Register(L"CompletionModelManager", [] {
      auto paths = MakeNonNullShared<std::vector<Path>>();
      auto GetManager = [paths] {
        return MakeNonNullUnique<CompletionModelManager>([paths](Path path) {
          LOG(INFO) << "Creating buffer for: " << path;
          paths->push_back(path);
          gc::Root<OpenBuffer> output = OpenBuffer::New(
              {.editor = EditorForTests(),
               .name = EditorForTests().GetUnusedBufferName(L"test buffer")});
          output.ptr()->AppendToLastLine(NewLazyString(L"bb baby"));
          output.ptr()->AppendRawLine(MakeNonNullShared<Line>(L"f fox"));
          output.ptr()->AppendRawLine(MakeNonNullShared<Line>(L"i i"));
          return futures::Past(output);
        });
      };
      return std::vector<tests::Test>(
          {{.name = L"Creation",
            .callback =
                [GetManager, paths] {
                  GetManager();
                  CHECK(paths->empty());
                }},
           {.name = L"SimpleQueryNothing",
            .callback =
                [GetManager, paths] {
                  CHECK(std::holds_alternative<
                        CompletionModelManager::NothingFound>(
                      GetManager()
                          ->Query({ValueOrDie(Path::FromString(L"en"))},
                                  CompletionModelManager::CompressedText(
                                      NewLazyString(L"nothing")))
                          .Get()
                          .value()));
                  CHECK(paths.value() ==
                        std::vector<Path>{ValueOrDie(Path::FromString(L"en"))});
                }},
           {.name = L"SimpleQueryWithMatch",
            .callback =
                [GetManager, paths] {
                  CompletionModelManager::QueryOutput output =
                      GetManager()
                          ->Query({ValueOrDie(Path::FromString(L"en"))},
                                  CompletionModelManager::CompressedText(
                                      NewLazyString(L"f")))
                          .Get()
                          .value();
                  CHECK(paths.value() ==
                        std::vector<Path>{ValueOrDie(Path::FromString(L"en"))});
                  CHECK(
                      std::get<CompletionModelManager::Text>(output).value() ==
                      CompletionModelManager::Text(NewLazyString(L"fox"))
                          .value());
                }},
           {.name = L"SimpleQueryWithReverseMatch",
            .callback =
                [GetManager, paths] {
                  CompletionModelManager::QueryOutput output =
                      GetManager()
                          ->Query({ValueOrDie(Path::FromString(L"en"))},
                                  CompletionModelManager::CompressedText(
                                      NewLazyString(L"fox")))
                          .Get()
                          .value();
                  CHECK(paths.value() ==
                        std::vector<Path>{ValueOrDie(Path::FromString(L"en"))});
                  CHECK(std::get<CompletionModelManager::Suggestion>(output)
                            .compressed_text.value() ==
                        CompletionModelManager::CompressedText(
                            NewLazyString(L"f"))
                            .value());
                }},
           {.name = L"RepeatedQuerySameModel", .callback = [GetManager, paths] {
              NonNull<std::unique_ptr<CompletionModelManager>> manager =
                  GetManager();
              std::vector<Path> input_paths = {
                  ValueOrDie(Path::FromString(L"en"))};
              for (int i = 0; i < 10; i++) {
                CHECK(std::get<CompletionModelManager::CompressedText>(
                          manager
                              ->Query(input_paths,
                                      CompletionModelManager::CompressedText(
                                          NewLazyString(L"f")))
                              .Get()
                              .value())
                          .value() ==
                      CompletionModelManager::Text(NewLazyString(L"fox"))
                          .value());
              }
              // The gist of the test is here:
              CHECK_EQ(paths->size(), 1ul);
            }}});
      // TODO(trivial, 2023-09-08): Add more tests. Test calls with multiple
      // models: the first model in the order should take precedence. Also
      // assert that repeated models are OK and won't cause problems.
    }());
}  // namespace
}  // namespace afc::editor
