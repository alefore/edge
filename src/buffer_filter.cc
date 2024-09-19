#include "src/buffer_filter.h"

#include <glog/logging.h>

#include "src/infrastructure/dirname.h"
#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/overload.h"
#include "src/language/text/line_builder.h"
#include "src/math/naive_bayes.h"
#include "src/tests/tests.h"
#include "src/vm/escape.h"

namespace container = afc::language::container;

using afc::futures::DeleteNotification;
using afc::infrastructure::Path;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::IsError;
using afc::language::overload;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::FindFirstColumnWithPredicate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::lazy_string::TokenizeNameForPrefixSearches;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::vm::EscapedMap;
using afc::vm::EscapedString;
using afc::vm::Identifier;

namespace afc::editor {
const vm::Identifier& HistoryIdentifierValue() {
  static const vm::Identifier* output =
      new Identifier{NonEmptySingleLine{SingleLine{LazyString{L"value"}}}};
  return *output;
}

const vm::Identifier& HistoryIdentifierExtension() {
  static const vm::Identifier* output =
      new Identifier{NonEmptySingleLine{SingleLine{LazyString{L"extension"}}}};
  return *output;
}

const vm::Identifier& HistoryIdentifierName() {
  static const vm::Identifier* output =
      new Identifier{NonEmptySingleLine{SingleLine{LazyString{L"name"}}}};
  return *output;
}

const vm::Identifier& HistoryIdentifierActive() {
  static const vm::Identifier* output =
      new Identifier{NonEmptySingleLine{SingleLine{LazyString{L"active"}}}};
  return *output;
}

const vm::Identifier& HistoryIdentifierDirectory() {
  static const vm::Identifier* output =
      new Identifier{NonEmptySingleLine{SingleLine{LazyString{L"directory"}}}};
  return *output;
}

namespace {
// Generates additional features that are derived from the features returned by
// GetCurrentFeatures (and thus don't need to be saved).
std::multimap<Identifier, EscapedString> GetSyntheticFeatures(
    const std::multimap<Identifier, EscapedString>& input) {
  TRACK_OPERATION(FilterSortBuffer_GetSyntheticFeatures);

  std::multimap<Identifier, EscapedString> output;
  std::unordered_set<Path> directories;
  std::unordered_set<LazyString> extensions;
  VLOG(5) << "Generating features from input: " << input.size();
  for (const auto& [name, value] : input) {
    if (name == HistoryIdentifierName()) {
      std::visit(
          overload{
              IgnoreErrors{},
              [&](const Path& path) {
                std::visit(overload{IgnoreErrors{},
                                    [&](Path directory) {
                                      if (directory != Path::LocalDirectory())
                                        directories.insert(directory);
                                    }},
                           path.Dirname());
                VisitOptional(
                    [&](LazyString extension) { extensions.insert(extension); },
                    [] {}, path.extension());
              }},
          Path::New(value.OriginalString()));
    }
  }

  VLOG(5) << "Generating features from directories.";
  for (auto& dir : directories)
    output.insert({HistoryIdentifierDirectory(), EscapedString{dir.read()}});

  VLOG(5) << "Generating features from extensions.";
  for (const LazyString& extension : extensions)
    output.insert({HistoryIdentifierExtension(), EscapedString{extension}});

  VLOG(5) << "Done generating synthetic features.";
  return output;
}

const bool get_synthetic_features_tests_registration = tests::Register(
    L"GetSyntheticFeaturesTests",
    {{.name = L"Empty",
      .callback = [] { CHECK_EQ(GetSyntheticFeatures({}).size(), 0ul); }},
     {.name = L"ExtensionsSimple",
      .callback =
          [] {
            std::multimap<Identifier, EscapedString> input;
            input.insert({HistoryIdentifierName(),
                          EscapedString{LazyString{L"foo.cc"}}});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(HistoryIdentifierExtension()), 1ul);
            CHECK_EQ(output.find(HistoryIdentifierExtension())->second,
                     EscapedString{LazyString{L"cc"}});
          }},
     {.name = L"ExtensionsLongDirectory",
      .callback =
          [] {
            std::multimap<Identifier, EscapedString> input;
            input.insert(
                {HistoryIdentifierName(),
                 EscapedString{LazyString{L"/home/alejo/src/edge/foo.cc"}}});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(HistoryIdentifierExtension()), 1ul);
            CHECK_EQ(output.find(HistoryIdentifierExtension())->second,
                     EscapedString{LazyString{L"cc"}});
          }},
     {.name = L"ExtensionsMultiple",
      .callback =
          [] {
            std::multimap<Identifier, EscapedString> input;
            input.insert({HistoryIdentifierName(),
                          EscapedString{LazyString{L"/home/alejo/foo.cc"}}});
            input.insert({HistoryIdentifierName(),
                          EscapedString{LazyString{L"bar.cc"}}});
            input.insert({HistoryIdentifierName(),
                          EscapedString{LazyString{L"/home/alejo/buffer.h"}}});
            input.insert({HistoryIdentifierName(),
                          EscapedString{LazyString{L"/home/alejo/README.md"}}});
            auto output = GetSyntheticFeatures(input);
            auto range = output.equal_range(HistoryIdentifierExtension());
            CHECK_EQ(std::distance(range.first, range.second), 3l);
            while (range.first != range.second) {
              EscapedString value = range.first->second;
              CHECK(value == EscapedString{LazyString{L"cc"}} ||
                    value == EscapedString{LazyString{L"h"}} ||
                    value == EscapedString{LazyString{L"md"}});
              ++range.first;
            }
          }},
     {.name = L"DirectoryPlain",
      .callback =
          [] {
            std::multimap<Identifier, EscapedString> input;
            input.insert({HistoryIdentifierName(),
                          EscapedString{LazyString{L"foo.cc"}}});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(HistoryIdentifierDirectory()), 0ul);
          }},
     {.name = L"DirectoryPath",
      .callback =
          [] {
            std::multimap<Identifier, EscapedString> input;
            input.insert(
                {HistoryIdentifierName(),
                 EscapedString{LazyString{L"/home/alejo/edge/foo.cc"}}});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(HistoryIdentifierDirectory()), 1ul);
            CHECK_EQ(output.find(HistoryIdentifierDirectory())->second,
                     EscapedString{LazyString{L"/home/alejo/edge"}});
          }},
     {.name = L"DirectoryMultiple", .callback = [] {
        std::multimap<Identifier, EscapedString> input;
        input.insert({HistoryIdentifierName(),
                      EscapedString{LazyString{L"/home/alejo/edge/foo.cc"}}});
        input.insert({HistoryIdentifierName(),
                      EscapedString{LazyString{L"/home/alejo/edge/bar.cc"}}});
        input.insert({HistoryIdentifierName(),
                      EscapedString{LazyString{L"/home/alejo/btc/input.txt"}}});
        auto output = GetSyntheticFeatures(input);
        auto range = output.equal_range(HistoryIdentifierDirectory());
        CHECK_EQ(std::distance(range.first, range.second), 2l);
        while (range.first != range.second) {
          EscapedString value = range.first->second;
          CHECK(value == EscapedString{LazyString{L"/home/alejo/edge"}} ||
                value == EscapedString{LazyString{L"/home/alejo/btc"}});
          ++range.first;
        }
      }}});

ValueOrError<std::multimap<Identifier, EscapedString>> ParseBufferLine(
    const Line& line) {
  TRACK_OPERATION(FilterSortBuffer_ParseBufferLine);
  DECLARE_OR_RETURN(EscapedMap line_map, line.escaped_map());
  std::multimap<Identifier, EscapedString> output = line_map.read();
  std::multimap<Identifier, EscapedString> synthetic_features =
      GetSyntheticFeatures(output);
  output.insert(synthetic_features.begin(), synthetic_features.end());
  return output;
}

auto parse_history_line_tests_registration = tests::Register(
    L"ParseBufferLine",
    {{.name = L"BadQuote",
      .callback =
          [] {
            CHECK(IsError(ParseBufferLine(
                LineBuilder{SingleLine{LazyString{L"value:\""}}}.Build())));
          }},
     {.name = L"Empty", .callback = [] {
        auto result = ValueOrDie(ParseBufferLine(
            LineBuilder{SingleLine{LazyString{L"value:\"\""}}}.Build()));
        CHECK_EQ(result.find(HistoryIdentifierValue())->second,
                 EscapedString{});
      }}});

auto quote_string_tests_registration = tests::Register(L"QuoteString", [] {
  auto test = [](std::wstring name, std::wstring input,
                 std::wstring expected_output) {
    return tests::Test(
        {.name = name, .callback = [=] {
           CHECK_EQ(EscapedString(LazyString{input}).CppRepresentation(),
                    SingleLine{LazyString(expected_output)});
         }});
  };
  return std::vector<tests::Test>(
      {test(L"Simple", L"alejo", L"\"alejo\""),
       test(L"DoubleQuotes", L"alejo \"is\" here",
            L"\"alejo \\\"is\\\" here\""),
       test(L"QuoteAtStart", L"\"foo bar", L"\"\\\"foo bar\""),
       test(L"QuoteAtEnd", L"foo\"", L"\"foo\\\"\""),
       test(L"MultiLine", L"foo\nbar\nhey", L"\"foo\\nbar\\nhey\"")});
}());
}  // namespace

Line ColorizeLine(LazyString line, std::vector<TokenAndModifiers> tokens) {
  TRACK_OPERATION(FilterSortBuffer_ColorizeLine);
  sort(tokens.begin(), tokens.end(),
       [](const TokenAndModifiers& a, const TokenAndModifiers& b) {
         return a.token.begin < b.token.begin;
       });
  VLOG(6) << "Producing output: " << line;
  LineBuilder options;
  ColumnNumber position;
  auto push_to_position = [&](ColumnNumber end, LineModifierSet modifiers) {
    if (end <= position) return;
    VLOG(8) << "Adding substring with modifiers: " << position << ", "
            << modifiers;
    options.AppendString(line.Substring(position, end - position),
                         std::move(modifiers));
    position = end;
  };
  for (const auto& t : tokens) {
    push_to_position(t.token.begin, {});
    push_to_position(t.token.end, t.modifiers);
  }
  push_to_position(ColumnNumber() + line.size(), {});
  return std::move(options).Build();
}

bool operator==(const FilterSortBufferOutput::Match& a,
                const FilterSortBufferOutput::Match& b) {
  return a.preview == b.preview && a.data == b.data;
}

std::ostream& operator<<(std::ostream& os,
                         const FilterSortBufferOutput::Match& m) {
  os << "[" << m.preview.contents() << "]:(" << m.data.ToLazyString() << ")";
  return os;
}

FilterSortBufferOutput FilterSortBuffer(FilterSortBufferInput input) {
  VLOG(4) << "Start matching: " << input.history.size();
  TRACK_OPERATION(FilterSortBuffer);
  FilterSortBufferOutput output;

  if (input.abort_value.has_value()) return output;
  // Sets of features for each unique `value` value in the history.
  math::naive_bayes::History history_data;
  // Tokens by parsing the `value` value in the history.
  std::unordered_map<math::naive_bayes::Event, std::vector<Token>>
      history_value_tokens;
  std::vector<Token> filter_tokens = TokenizeBySpaces(input.filter);
  input.history.EveryLine([&](LineNumber, const Line& line) {
    TRACK_OPERATION(FilterSortBuffer_Input_History_EveryLine);
    VLOG(8) << "Considering line: " << line.contents();
    auto warn_if = [&](bool condition, Error error) {
      if (condition) {
        // We don't use AugmentError because we'd rather append to the
        // end of the description, not the beginning.
        Error wrapper_error{error.read() + LazyString{L": "} +
                            line.contents().read()};
        VLOG(5) << "Found error: " << wrapper_error;
        output.errors.push_back(wrapper_error);
      }
      return condition;
    };
    if (line.empty()) return true;
    ValueOrError<std::multimap<Identifier, EscapedString>> line_keys_or_error =
        ParseBufferLine(line);
    auto* line_keys = std::get_if<0>(&line_keys_or_error);
    if (line_keys == nullptr) {
      output.errors.push_back(std::get<Error>(line_keys_or_error));
      return !input.abort_value.has_value();
    }
    auto range = line_keys->equal_range(HistoryIdentifierValue());
    int value_count = std::distance(range.first, range.second);
    if (warn_if(value_count == 0,
                Error{LazyString{L"Line is missing `value` section"}}) ||
        warn_if(value_count != 1,
                Error{LazyString{L"Line has multiple `value` sections"}})) {
      return !input.abort_value.has_value();
    }

    EscapedString history_value = range.first->second;
    VLOG(8) << "Considering history value: " << history_value;
    std::vector<Token> line_tokens = ExtendTokensToEndOfString(
        history_value.EscapedRepresentation(),
        TokenizeNameForPrefixSearches(
            history_value.EscapedRepresentation().read()));
    math::naive_bayes::Event event_key(
        ToLazyString(history_value.EscapedRepresentation()));
    std::vector<math::naive_bayes::FeaturesSet>* features_output = nullptr;
    if (filter_tokens.empty()) {
      VLOG(6) << "Accepting value (empty filters): " << line.contents();
      features_output = &history_data[event_key];
    } else if (auto match = FindFilterPositions(filter_tokens, line_tokens);
               match.has_value()) {
      VLOG(5) << "Accepting value, produced a match: " << line.contents();
      features_output = &history_data[event_key];
      history_value_tokens.insert({event_key, std::move(match.value())});
    } else {
      VLOG(6) << "Ignoring value, no match: " << line.contents();
      return true;
    }
    math::naive_bayes::FeaturesSet current_features;
    for (auto& [key, value] : *line_keys)
      if (key != HistoryIdentifierValue())
        current_features.insert(math::naive_bayes::Feature(
            ToLazyString(key) + LazyString{L":"} +
            ToLazyString(value.EscapedRepresentation())));
    features_output->push_back(std::move(current_features));

    return !input.abort_value.has_value();
  });

  VLOG(4) << "Matches found: " << history_data.read().size();

  // For sorting.
  math::naive_bayes::FeaturesSet current_features;
  for (const auto& [name, value] : input.current_features)
    current_features.insert(
        math::naive_bayes::Feature{ToLazyString(name) + LazyString{L":"} +
                                   ToLazyString(value.CppRepresentation())});
  for (const auto& [name, value] : GetSyntheticFeatures(input.current_features))
    current_features.insert(
        math::naive_bayes::Feature{ToLazyString(name) + LazyString{L":"} +
                                   ToLazyString(value.CppRepresentation())});

  for (math::naive_bayes::Event& key :
       math::naive_bayes::Sort(history_data, current_features))
    if (ValueOrError<EscapedString> data = EscapedString::Parse(key.read());
        !IsError(data))
      output.matches.push_back(FilterSortBufferOutput::Match{
          .preview = ColorizeLine(
              key.read(), container::MaterializeVector(
                              history_value_tokens[key] |
                              std::views::transform([](const Token& token) {
                                VLOG(6) << "Add token BOLD: " << token;
                                return TokenAndModifiers{
                                    token,
                                    LineModifierSet{LineModifier::kCyan}};
                              }))),
          .data = LineSequence::BreakLines(
              ValueOrDie(std::move(data)).OriginalString())});
  return output;
}

namespace {
auto filter_sort_history_sync_tests_registration = tests::Register(
    L"FilterSortBuffer",
    {
        {.name = L"EmptyFilterEmptyHistory",
         .callback =
             [] {
               std::multimap<Identifier, EscapedString> features;
               FilterSortBufferOutput output =
                   FilterSortBuffer(FilterSortBufferInput{
                       DeleteNotification::Never(), SingleLine{},
                       LineSequence(), features});
               CHECK(output.matches.empty());
             }},
        {.name = L"EmptyFilterHistory",
         .callback =
             [] {
               std::multimap<Identifier, EscapedString> features;
               FilterSortBufferOutput output =
                   FilterSortBuffer(FilterSortBufferInput{
                       DeleteNotification::Never(), SingleLine{LazyString{L""}},
                       container::Materialize<LineSequence>(std::vector<Line>{
                           LineBuilder{SingleLine{LazyString{L"value:\"foo\""}}}
                               .Build(),
                           LineBuilder{
                               SingleLine{LazyString{L"value:\"bar\\n\""}}}
                               .Build()}),
                       features});
               CHECK_EQ(output.matches.size(), 2ul);
               // TODO(2024-09-17): This is brittle, the order of the results
               // could change.
               CHECK_EQ(output.matches[0],
                        (FilterSortBufferOutput::Match{
                            .preview = Line{SingleLine{LazyString{L"bar\\n"}}},
                            .data = LineSequence::ForTests({L"bar", L""})}));
             }},
        {.name = L"NoMatch",
         .callback =
             [] {
               std::multimap<Identifier, EscapedString> features;
               FilterSortBufferOutput output =
                   FilterSortBuffer(FilterSortBufferInput{
                       DeleteNotification::Never(),
                       SingleLine{LazyString{L"quux"}},
                       container::Materialize<LineSequence>(std::vector<Line>{
                           LineBuilder{
                               SingleLine{LazyString{L"value:\"foobar\""}}}
                               .Build(),
                           LineBuilder{SingleLine{LazyString{L"value:\"foo\""}}}
                               .Build()}),
                       features});
               CHECK(output.matches.empty());
             }},
        {.name = L"MatchAfterEscape",
         .callback =
             [] {
               std::multimap<Identifier, EscapedString> features;
               FilterSortBufferOutput output =
                   FilterSortBuffer(FilterSortBufferInput{
                       DeleteNotification::Never(),
                       SingleLine{LazyString{L"nbar"}},
                       LineSequence::WithLine(LineBuilder{
                           SingleLine{LazyString{L"value:\"foo\\nbardo\""}}}
                                                  .Build()),
                       features});
               CHECK_EQ(output.matches.size(), 1ul);

               LineBuilder expected_preview{SingleLine{LazyString{L"foo\\"}}};
               expected_preview.AppendString(
                   LazyString{L"nbar"}, LineModifierSet{LineModifier::kCyan});
               expected_preview.AppendString(LazyString{L"do"});

               CHECK_EQ(
                   output.matches[0],
                   (FilterSortBufferOutput::Match{
                       .preview = std::move(expected_preview).Build(),
                       .data = LineSequence::ForTests({L"foo", L"bardo"})}));
             }},
        {.name = L"MatchIncludingEscapeCorrectlyHandled",
         .callback =
             [] {
               std::multimap<Identifier, EscapedString> features;
               FilterSortBufferOutput output =
                   FilterSortBuffer(FilterSortBufferInput{
                       DeleteNotification::Never(),
                       SingleLine{LazyString{L"nbar"}},
                       LineSequence::WithLine(LineBuilder{
                           SingleLine{LazyString{L"value:\"foo\\nbar\""}}}
                                                  .Build()),
                       features});
               CHECK_EQ(output.matches.size(), 1ul);
               CHECK_EQ(output.matches[0].preview.contents(),
                        SingleLine{LazyString{L"foo\\nbar"}});
             }},
        {.name = L"IgnoresInvalidEntries",
         .callback =
             [] {
               std::multimap<Identifier, EscapedString> features;
               FilterSortBufferOutput output =
                   FilterSortBuffer(FilterSortBufferInput{
                       DeleteNotification::Never(),
                       SingleLine{LazyString{L"f"}},
                       container::Materialize<LineSequence>(std::vector<Line>{
                           LineBuilder{
                               SingleLine{LazyString{L"value:\"foobar \\\""}}}
                               .Build(),
                           LineBuilder{SingleLine{LazyString{L"value:\"foo\""}}}
                               .Build(),
                           LineBuilder{
                               SingleLine{LazyString{L"value:\"foo\\n bar\""}}}
                               .Build(),
                           LineBuilder{SingleLine{LazyString{
                                           L"value:\"foo \\o bar \\\""}}}
                               .Build(),
                       }),
                       features});
               CHECK_EQ(output.matches.size(), 2ul);
               // TODO(2024-09-17): This is brittle, the order of the results
               // could change.
               CHECK_EQ(output.matches[0].preview.contents(),
                        SingleLine{LazyString{L"foo\\n bar"}});
               CHECK_EQ(output.matches[1].preview.contents(),
                        SingleLine{LazyString{L"foo"}});
             }},
        {.name = L"HistoryWithNewLine",
         .callback =
             [] {
               std::multimap<Identifier, EscapedString> features;
               FilterSortBufferOutput output =
                   FilterSortBuffer(FilterSortBufferInput{
                       DeleteNotification::Never(),
                       SingleLine{LazyString{L"ls"}},
                       LineSequence::WithLine(LineBuilder{
                           SingleLine{LazyString{L"value:\"ls\\n\""}}}
                                                  .Build()),
                       features});
               CHECK_EQ(output.matches.size(), 1ul);

               LineBuilder expected_preview;
               expected_preview.AppendString(
                   LazyString{L"ls"}, LineModifierSet{LineModifier::kCyan});
               expected_preview.AppendString(LazyString{L"\\n"});

               CHECK_EQ(output.matches[0],
                        (FilterSortBufferOutput::Match{
                            .preview = std::move(expected_preview).Build(),
                            .data = LineSequence::ForTests({L"ls", L""})}));
             }},
    });
}  // namespace
}  // namespace afc::editor
