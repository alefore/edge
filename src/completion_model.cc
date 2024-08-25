#include "src/completion_model.h"

#include "src/language/error/view.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/text/line_sequence_functional.h"
#include "src/language/text/mutable_line_sequence.h"
#include "src/language/text/sorted_line_sequence.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;

using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::FindFirstColumnWithPredicate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::LowerCase;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::language::text::SortedLineSequence;

namespace afc::editor {
using ::operator<<;

namespace {
struct ParsedLine {
  DictionaryManager::Key compressed_text;
  DictionaryManager::WordData word_data;
};

ValueOrError<ParsedLine> Parse(const Line& line) {
  return VisitOptional(
      [&line](ColumnNumber first_space) -> ValueOrError<ParsedLine> {
        return ParsedLine{
            .compressed_text = DictionaryManager::Key(
                line.Substring(ColumnNumber(), first_space.ToDelta())),
            .word_data = DictionaryManager::WordData{
                .replacement =
                    line.Substring(first_space + ColumnNumberDelta(1))}};
      },
      [] {
        return ValueOrError<ParsedLine>(Error{LazyString{L"No space found."}});
      },
      FindFirstColumnWithPredicate(
          line.contents(), [](ColumnNumber, wchar_t c) { return c == L' '; }));
}

SortedLineSequence PrepareBuffer(LineSequence input) {
  TRACK_OPERATION(CompletionModel_PrepareBuffer_sort);
  return SortedLineSequence(FilterLines(input, [](const Line& line) {
    return line.contents().size().IsZero()
               ? language::text::FilterPredicateResult::kErase
               : language::text::FilterPredicateResult::kKeep;
  }));
}

SortedLineSequence CompletionModelForTests() {
  return PrepareBuffer(
      LineSequence::ForTests({L"", L"bb baby", L"f fox", L"", L"", L"i i"}));
}

const bool prepare_buffer_tests_registration = tests::Register(
    L"DictionaryManager::PrepareBuffer",
    {{.name = L"EmptyBuffer",
      .callback =
          [] {
            CHECK(PrepareBuffer(LineSequence()).lines().ToString() == L"");
          }},
     {.name = L"UnsortedBuffer", .callback = [] {
        LineSequence contents =
            LineSequence::ForTests({L"", L"f fox", L"", L"", L"b baby", L""});
        CHECK(contents.ToString() == L"\nf fox\n\n\nb baby\n");
        std::wstring result =
            PrepareBuffer(std::move(contents)).lines().ToString();
        LOG(INFO) << "Result: [" << result << "]";
        CHECK(result == L"b baby\nf fox");
      }}});

std::optional<DictionaryManager::WordData> FindCompletionInModel(
    const SortedLineSequence& contents,
    const DictionaryManager::Key& compressed_text) {
  VLOG(3) << "Starting completion with model with size: "
          << contents.lines().size() << " token: " << compressed_text;
  LineNumber line =
      contents.upper_bound(LineBuilder(compressed_text.read()).Build());

  if (line > contents.lines().EndLine()) return std::nullopt;

  LazyString line_contents = contents.lines().at(line).contents();
  // TODO(easy, 2023-09-01): Avoid calls to ToString, ugh.
  VLOG(5) << "Check: " << compressed_text
          << " against: " << line_contents.ToString();
  return std::visit(
      overload{
          [&](const ParsedLine& parsed_line)
              -> std::optional<DictionaryManager::WordData> {
            if (compressed_text != parsed_line.compressed_text) {
              VLOG(5) << "No match: [" << compressed_text << "] != ["
                      << parsed_line.compressed_text << "]";
              return std::nullopt;
            }

            if (compressed_text == parsed_line.word_data.replacement) {
              VLOG(4) << "Found a match, but the line has compressed text "
                         "identical to parsed text, so we'll skip it.";
              return std::nullopt;
            }

            VLOG(2) << "Found compression: " << parsed_line.compressed_text
                    << " -> "
                    << parsed_line.word_data.replacement.value().ToString();
            return parsed_line.word_data;
          },
          [](Error) { return std::optional<DictionaryManager::WordData>(); }},
      Parse(contents.lines().at(line)));
}

const bool find_completion_tests_registration = tests::Register(
    L"completion::FindCompletionInModel",
    {{.name = L"EmptyModel",
      .callback =
          [] {
            CHECK(FindCompletionInModel(SortedLineSequence(LineSequence()),
                                        DictionaryManager::Key(LazyString{
                                            L"foo"})) == std::nullopt);
          }},
     {.name = L"NoMatch",
      .callback =
          [] {
            CHECK(FindCompletionInModel(CompletionModelForTests(),
                                        DictionaryManager::Key(LazyString{
                                            L"foo"})) == std::nullopt);
          }},
     {.name = L"Match",
      .callback =
          [] {
            CHECK(
                FindCompletionInModel(CompletionModelForTests(),
                                      DictionaryManager::Key(LazyString{L"f"}))
                    .value()
                    .replacement.value() == LazyString{L"fox"});
          }},
     {.name = L"IdenticalMatch", .callback = [] {
        CHECK(FindCompletionInModel(CompletionModelForTests(),
                                    DictionaryManager::Key(LazyString{L"i"})) ==
              std::nullopt);
      }}});
}  // namespace

bool DictionaryManager::WordData::operator==(const WordData& other) const {
  return replacement == other.replacement;
}

DictionaryManager::DictionaryManager(BufferLoader buffer_loader)
    : buffer_loader_(std::move(buffer_loader)) {}

futures::Value<DictionaryManager::QueryOutput> DictionaryManager::Query(
    std::vector<Path> models, DictionaryManager::Key compressed_text) {
  return FindWordDataWithIndex(
      buffer_loader_, data_,
      std::make_shared<std::vector<Path>>(std::move(models)),
      std::move(compressed_text), 0);
}

/* static */
futures::Value<DictionaryManager::QueryOutput>
DictionaryManager::FindWordDataWithIndex(
    BufferLoader buffer_loader,
    NonNull<std::shared_ptr<concurrent::Protected<Data>>> data,
    std::shared_ptr<std::vector<Path>> models_list,
    DictionaryManager::Key compressed_text, size_t index) {
  if (index == models_list->size())
    return futures::Past(
        data->lock([&](const Data& locked_data) -> QueryOutput {
          WordData text{.replacement = compressed_text.read()};
          if (auto text_it = locked_data.reverse_table.find(
                  text.replacement.value().ToString());
              text_it != locked_data.reverse_table.end()) {
            for (const Path& path : *models_list)
              if (auto path_it = text_it->second.find(path);
                  path_it != text_it->second.end())
                return Suggestion{.key = path_it->second};
          }
          return NothingFound{};
        }));

  futures::ListenableValue<SortedLineSequence> current_future =
      data->lock([&](Data& locked_data) {
        Path path = models_list->at(index);
        if (auto it = locked_data.models.find(path);
            it != locked_data.models.end())
          return it->second;
        auto output =
            locked_data.models
                .insert(
                    {path, futures::ListenableValue<SortedLineSequence>(
                               buffer_loader(path).Transform(PrepareBuffer))})
                .first->second;
        // TODO(P2, 2023-09-08, RaceCondition): There's a race here where output
        // may get a value after this check but before the execution of
        // AddListener below. If that happens, we'll deadlock. Figure out a
        // better solution.
        if (output.has_value()) {
          UpdateReverseTable(locked_data, path,
                             output.get_copy().value().lines());
        } else {
          LOG(INFO) << "Adding listener to update reverse table.";
          output.AddListener([data, path](const SortedLineSequence& contents) {
            LOG(INFO) << "Updating reverse table.";
            data->lock([&](Data& data_locked) {
              UpdateReverseTable(data_locked, path, contents.lines());
            });
          });
        }
        return output;
      });

  return std::move(current_future)
      .ToFuture()
      .Transform([buffer_loader = std::move(buffer_loader),
                  data = std::move(data), models_list = std::move(models_list),
                  compressed_text, index](SortedLineSequence contents) mutable {
        if (std::optional<WordData> result =
                FindCompletionInModel(contents, compressed_text);
            result.has_value())
          return futures::Past(QueryOutput(*result));
        return FindWordDataWithIndex(buffer_loader, std::move(data),
                                     std::move(models_list), compressed_text,
                                     index + 1);
      });
}

/* static */ void DictionaryManager::UpdateReverseTable(
    Data& data, const Path& path, const LineSequence& contents) {
  std::ranges::for_each(
      contents | std::views::transform(Parse) | language::view::SkipErrors |
          std::views::filter([](const ParsedLine& entry) {
            return entry.word_data.replacement.value() != entry.compressed_text;
          }),
      [&path, &data](const ParsedLine& entry) {
        data.reverse_table[entry.word_data.replacement.value().ToString()]
            .insert({path, entry.compressed_text});
      });
}

namespace {
const bool completion_model_manager_tests_registration =
    tests::Register(L"DictionaryManager", [] {
      auto paths = MakeNonNullShared<std::vector<Path>>();
      auto GetManager = [paths] {
        return MakeNonNullUnique<DictionaryManager>([paths](Path path) {
          LOG(INFO) << "Creating buffer for: " << path;
          paths->push_back(path);
          if (path == ValueOrDie(Path::New(LazyString{L"en"}))) {
            return futures::Past(
                LineSequence::ForTests({L"", L"bb baby", L"f fox", L"i i"}));
          } else {
            return futures::Past(
                LineSequence::ForTests({L"", L"f firulais", L"p perrito"}));
          }
        });
      };
      auto TestQuery =
          [](const NonNull<std::unique_ptr<DictionaryManager>>& manager,
             const std::vector<std::wstring>& models,
             std::wstring compressed_text) {
            std::vector<infrastructure::Path> model_paths;
            for (const std::wstring& path : models)
              model_paths.push_back(ValueOrDie(Path::New(LazyString{path})));
            return manager
                ->Query(model_paths,
                        DictionaryManager::Key(LazyString{compressed_text}))
                .Get();
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
                [GetManager, TestQuery, paths] {
                  CHECK(std::holds_alternative<DictionaryManager::NothingFound>(
                      TestQuery(GetManager(), {L"en"}, L"nothing").value()));
                  CHECK(paths.value() == std::vector<Path>{ValueOrDie(
                                             Path::New(LazyString{L"en"}))});
                }},
           {.name = L"SimpleQueryWithMatch",
            .callback =
                [GetManager, TestQuery, paths] {
                  DictionaryManager::QueryOutput output =
                      TestQuery(GetManager(), {L"en"}, L"f").value();
                  CHECK(paths.value() == std::vector<Path>{ValueOrDie(
                                             Path::New(LazyString{L"en"}))});
                  CHECK(std::get<DictionaryManager::WordData>(output) ==
                        DictionaryManager::WordData{.replacement =
                                                        LazyString{L"fox"}});
                }},
           {.name = L"SimpleQueryWithReverseMatch",
            .callback =
                [GetManager, TestQuery, paths] {
                  DictionaryManager::QueryOutput output =
                      TestQuery(GetManager(), {L"en"}, L"fox").value();
                  CHECK(paths.value() == std::vector<Path>{ValueOrDie(
                                             Path::New(LazyString{L"en"}))});
                  CHECK(std::get<DictionaryManager::Suggestion>(output).key ==
                        DictionaryManager::Key(LazyString{L"f"}));
                }},
           {.name = L"RepeatedQuerySameModel",
            .callback =
                [GetManager, TestQuery, paths] {
                  const NonNull<std::unique_ptr<DictionaryManager>> manager =
                      GetManager();
                  for (int i = 0; i < 10; i++) {
                    CHECK(std::get<DictionaryManager::WordData>(
                              TestQuery(manager, {L"en"}, L"f").value())
                              .replacement == LazyString{L"fox"});
                  }
                  // The gist of the test is here:
                  CHECK_EQ(paths->size(), 1ul);
                }},
           {.name = L"MultiModelQuery",
            .callback = [GetManager, TestQuery, paths] {
              const NonNull<std::unique_ptr<DictionaryManager>> manager =
                  GetManager();
              CHECK(std::get<DictionaryManager::WordData>(
                        TestQuery(manager, {L"en", L"en"}, L"f").value())
                        .replacement == LazyString{L"fox"});
              CHECK(std::get<DictionaryManager::WordData>(
                        TestQuery(manager, {L"en", L"es"}, L"f").value())
                        .replacement == LazyString{L"fox"});
              CHECK(std::get<DictionaryManager::WordData>(
                        TestQuery(manager, {L"en", L"es"}, L"p").value())
                        .replacement == LazyString{L"perrito"});
              CHECK(std::holds_alternative<DictionaryManager::NothingFound>(
                  TestQuery(manager, {L"en", L"es"}, L"rock").value()));
    // This shows a bug in the implementation: we shouldn't give a
    // recommendation for a compressed-text that is unreachable (because a model
    // with higher priority takes precedence). So we have it uncommented for
    // now.
    // TODO(P2, 2023-09-08): Fix the bug and uncomment the test.
#if 0
              CHECK(
                  std::holds_alternative<DictionaryManager::NothingFound>(
                      TestQuery(manager, {L"en", L"es"}, L"firulais")));
#endif
              CHECK_EQ(paths->size(), 2ul);
            }}});
    }());
}  // namespace
}  // namespace afc::editor
