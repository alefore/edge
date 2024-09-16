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
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::vm::EscapedMap;
using afc::vm::Identifier;

namespace afc::editor {
const vm::Identifier& HistoryIdentifierValue() {
  static const vm::Identifier* output =
      new vm::Identifier{language::lazy_string::LazyString{L"value"}};
  return *output;
}

const vm::Identifier& HistoryIdentifierExtension() {
  static const vm::Identifier* output =
      new vm::Identifier{language::lazy_string::LazyString{L"extension"}};
  return *output;
}

const vm::Identifier& HistoryIdentifierName() {
  static const vm::Identifier* output =
      new vm::Identifier{language::lazy_string::LazyString{L"name"}};
  return *output;
}

const vm::Identifier& HistoryIdentifierActive() {
  static const vm::Identifier* output =
      new vm::Identifier{language::lazy_string::LazyString{L"active"}};
  return *output;
}

const vm::Identifier& HistoryIdentifierDirectory() {
  static const vm::Identifier* output =
      new vm::Identifier{language::lazy_string::LazyString{L"directory"}};
  return *output;
}

namespace {
// Generates additional features that are derived from the features returned by
// GetCurrentFeatures (and thus don't need to be saved).
std::multimap<Identifier, LazyString> GetSyntheticFeatures(
    const std::multimap<Identifier, LazyString>& input) {
  std::multimap<Identifier, LazyString> output;
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
          Path::New(value));
    }
  }

  VLOG(5) << "Generating features from directories.";
  for (auto& dir : directories)
    output.insert({HistoryIdentifierDirectory(), dir.read()});

  VLOG(5) << "Generating features from extensions.";
  for (const LazyString& extension : extensions)
    output.insert({HistoryIdentifierExtension(), extension});

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
            std::multimap<Identifier, LazyString> input;
            input.insert({HistoryIdentifierName(), LazyString{L"foo.cc"}});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(HistoryIdentifierExtension()), 1ul);
            CHECK_EQ(output.find(HistoryIdentifierExtension())->second,
                     LazyString{L"cc"});
          }},
     {.name = L"ExtensionsLongDirectory",
      .callback =
          [] {
            std::multimap<Identifier, LazyString> input;
            input.insert({HistoryIdentifierName(),
                          LazyString{L"/home/alejo/src/edge/foo.cc"}});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(HistoryIdentifierExtension()), 1ul);
            CHECK_EQ(output.find(HistoryIdentifierExtension())->second,
                     LazyString{L"cc"});
          }},
     {.name = L"ExtensionsMultiple",
      .callback =
          [] {
            std::multimap<Identifier, LazyString> input;
            input.insert(
                {HistoryIdentifierName(), LazyString{L"/home/alejo/foo.cc"}});
            input.insert({HistoryIdentifierName(), LazyString{L"bar.cc"}});
            input.insert(
                {HistoryIdentifierName(), LazyString{L"/home/alejo/buffer.h"}});
            input.insert({HistoryIdentifierName(),
                          LazyString{L"/home/alejo/README.md"}});
            auto output = GetSyntheticFeatures(input);
            auto range = output.equal_range(HistoryIdentifierExtension());
            CHECK_EQ(std::distance(range.first, range.second), 3l);
            while (range.first != range.second) {
              LazyString value = range.first->second;
              CHECK(value == LazyString{L"cc"} || value == LazyString{L"h"} ||
                    value == LazyString{L"md"});
              ++range.first;
            }
          }},
     {.name = L"DirectoryPlain",
      .callback =
          [] {
            std::multimap<Identifier, LazyString> input;
            input.insert({HistoryIdentifierName(), LazyString{L"foo.cc"}});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(HistoryIdentifierDirectory()), 0ul);
          }},
     {.name = L"DirectoryPath",
      .callback =
          [] {
            std::multimap<Identifier, LazyString> input;
            input.insert({HistoryIdentifierName(),
                          LazyString{L"/home/alejo/edge/foo.cc"}});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(HistoryIdentifierDirectory()), 1ul);
            CHECK_EQ(output.find(HistoryIdentifierDirectory())->second,
                     LazyString{L"/home/alejo/edge"});
          }},
     {.name = L"DirectoryMultiple", .callback = [] {
        std::multimap<Identifier, LazyString> input;
        input.insert(
            {HistoryIdentifierName(), LazyString{L"/home/alejo/edge/foo.cc"}});
        input.insert(
            {HistoryIdentifierName(), LazyString{L"/home/alejo/edge/bar.cc"}});
        input.insert({HistoryIdentifierName(),
                      LazyString{L"/home/alejo/btc/input.txt"}});
        auto output = GetSyntheticFeatures(input);
        auto range = output.equal_range(HistoryIdentifierDirectory());
        CHECK_EQ(std::distance(range.first, range.second), 2l);
        while (range.first != range.second) {
          LazyString value = range.first->second;
          CHECK(value == LazyString{L"/home/alejo/edge"} ||
                value == LazyString{L"/home/alejo/btc"});
          ++range.first;
        }
      }}});

ValueOrError<std::multimap<Identifier, LazyString>> ParseBufferLine(
    const SingleLine& line) {
  DECLARE_OR_RETURN(EscapedMap line_map, EscapedMap::Parse(line.read()));
  std::multimap<Identifier, LazyString> output = line_map.read();
  std::multimap<Identifier, LazyString> synthetic_features =
      GetSyntheticFeatures(output);
  output.insert(synthetic_features.begin(), synthetic_features.end());
  return output;
}

auto parse_history_line_tests_registration = tests::Register(
    L"ParseBufferLine",
    {{.name = L"BadQuote",
      .callback =
          [] {
            CHECK(
                IsError(ParseBufferLine(SingleLine{LazyString{L"value:\""}})));
          }},
     {.name = L"Empty", .callback = [] {
        auto result =
            ValueOrDie(ParseBufferLine(SingleLine{LazyString{L"value:\"\""}}));
        CHECK_EQ(result.find(HistoryIdentifierValue())->second, LazyString{});
      }}});

// TODO(easy, 2022-06-03): Get rid of this? Just call EscapedString directly?
LazyString QuoteString(LazyString src) {
  return vm::EscapedString::FromString(src).CppRepresentation();
}

auto quote_string_tests_registration = tests::Register(L"QuoteString", [] {
  auto test = [](std::wstring name, std::wstring input,
                 std::wstring expected_output) {
    return tests::Test({.name = name, .callback = [=] {
                          CHECK(QuoteString(LazyString{input}) ==
                                LazyString(expected_output));
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

FilterSortBufferOutput FilterSortBuffer(FilterSortBufferInput input) {
  VLOG(4) << "Start matching: " << input.history.size();
  INLINE_TRACKER(FilterSortBuffer);
  FilterSortBufferOutput output;

  if (input.abort_value.has_value()) return output;
  // Sets of features for each unique `value` value in the history.
  math::naive_bayes::History history_data;
  // Tokens by parsing the `value` value in the history.
  std::unordered_map<math::naive_bayes::Event, std::vector<Token>>
      history_value_tokens;
  std::vector<Token> filter_tokens = TokenizeBySpaces(input.filter);
  input.history.EveryLine([&](LineNumber, const Line& line) {
    INLINE_TRACKER(FilterSortBuffer_Input_History_EveryLine);
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
    ValueOrError<std::multimap<Identifier, LazyString>> line_keys_or_error =
        ParseBufferLine(line.contents());
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

    LazyString value = range.first->second;
    VLOG(8) << "Considering history value: " << value;
    if (FindFirstColumnWithPredicate(value, [](ColumnNumber, wchar_t c) {
          return c == L'\n';
        }).has_value()) {
      VLOG(5) << "Ignoring value that contains a new line character.";
      return true;
    }
    std::vector<Token> line_tokens =
        ExtendTokensToEndOfString(value, TokenizeNameForPrefixSearches(value));
    // TODO(easy, 2022-11-26): Get rid of call ToString.
    math::naive_bayes::Event event_key(value.ToString());
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
    // TODO(easy, 2024-09-10): Get rid of call ToString.
    for (auto& [key, value] : *line_keys)
      if (key != HistoryIdentifierValue())
        current_features.insert(math::naive_bayes::Feature(
            key.read().ToString() + L":" + QuoteString(value).ToString()));
    features_output->push_back(std::move(current_features));

    return !input.abort_value.has_value();
  });

  VLOG(4) << "Matches found: " << history_data.read().size();

  // For sorting.
  math::naive_bayes::FeaturesSet current_features;
  // TODO(trivial, 2024-09-10): Get rid of ToString:
  for (const auto& [name, value] : input.current_features)
    current_features.insert(math::naive_bayes::Feature(
        (name.read() + LazyString{L":"} + QuoteString(value)).ToString()));
  // TODO(trivial, 2024-09-10): Get rid of ToString:
  for (const auto& [name, value] : GetSyntheticFeatures(input.current_features))
    current_features.insert(math::naive_bayes::Feature(
        (name.read() + LazyString{L":"} + QuoteString(value)).ToString()));

  for (math::naive_bayes::Event& key :
       math::naive_bayes::Sort(history_data, current_features)) {
    output.lines.push_back(ColorizeLine(
        key.ReadLazyString(), container::MaterializeVector(
                                  history_value_tokens[key] |
                                  std::views::transform([](const Token& token) {
                                    VLOG(6) << "Add token BOLD: " << token;
                                    return TokenAndModifiers{
                                        token,
                                        LineModifierSet{LineModifier::kBold}};
                                  }))));
  }
  return output;
}

namespace {
auto filter_sort_history_sync_tests_registration = tests::Register(
    L"FilterSortBuffer",
    {{.name = L"EmptyFilter",
      .callback =
          [] {
            std::multimap<Identifier, LazyString> features;
            FilterSortBufferOutput output =
                FilterSortBuffer(FilterSortBufferInput{
                    DeleteNotification::Never(), LazyString{L""},
                    LineSequence(), features});
            CHECK(output.lines.empty());
          }},
     {.name = L"NoMatch",
      .callback =
          [] {
            std::multimap<Identifier, LazyString> features;
            FilterSortBufferOutput output =
                FilterSortBuffer(FilterSortBufferInput{
                    DeleteNotification::Never(), LazyString{L"quux"},
                    LineSequence::ForTests(
                        {L"value:\"foobar\"", L"value:\"foo\""}),
                    features});
            CHECK(output.lines.empty());
          }},
     {.name = L"MatchAfterEscape",
      .callback =
          [] {
            std::multimap<Identifier, LazyString> features;
            FilterSortBufferOutput output =
                FilterSortBuffer(FilterSortBufferInput{
                    DeleteNotification::Never(), LazyString{L"nbar"},
                    LineSequence::ForTests({L"value:\"foo\\\\nbardo\""}),
                    features});
            CHECK_EQ(output.lines.size(), 1ul);
            const Line& line = output.lines[0];
            CHECK_EQ(line.contents(), SingleLine{LazyString{L"foo\\nbardo"}});

            const std::map<ColumnNumber, LineModifierSet> modifiers =
                line.modifiers();
            for (const auto& m : modifiers) {
              LOG(INFO) << "Modifiers: " << m.first << ": " << m.second;
            }

            {
              auto s = GetValueOrDie(modifiers, ColumnNumber(4));
              LOG(INFO) << "Modifiers found: " << s;
              CHECK_EQ(s, LineModifierSet{LineModifier::kBold});
            }
            {
              auto s = GetValueOrDie(modifiers, ColumnNumber(8));
              CHECK_EQ(s, LineModifierSet{});
            }
          }},
     {.name = L"MatchIncludingEscapeCorrectlyHandled",
      .callback =
          [] {
            std::multimap<Identifier, LazyString> features;
            FilterSortBufferOutput output =
                FilterSortBuffer(FilterSortBufferInput{
                    DeleteNotification::Never(), LazyString{L"nbar"},
                    LineSequence::ForTests({L"value:\"foo\\nbar\""}),
                    features});
            CHECK(output.lines.empty());
          }},
     {.name = L"IgnoresInvalidEntries",
      .callback =
          [] {
            std::multimap<Identifier, LazyString> features;
            FilterSortBufferOutput output =
                FilterSortBuffer(FilterSortBufferInput{
                    DeleteNotification::Never(), LazyString{L"f"},
                    LineSequence::ForTests(
                        {L"value:\"foobar \\\"", L"value:\"foo\"",
                         L"value:\"foo\\n bar\"", L"value:\"foo \\o bar \\\""}),
                    features});
            CHECK_EQ(output.lines.size(), 1ul);
            CHECK_EQ(output.lines[0].contents(),
                     SingleLine{LazyString{L"foo"}});
          }},
     {.name = L"HistoryWithRawNewLine",
      .callback =
          [] {
            std::multimap<Identifier, LazyString> features;
            FilterSortBufferOutput output =
                FilterSortBuffer(FilterSortBufferInput{
                    DeleteNotification::Never(), LazyString{L"ls"},
                    LineSequence::ForTests({L"value:\"ls\\n\""}), features});
            CHECK(output.lines.empty());
          }},
     {.name = L"HistoryWithEscapedNewLine", .callback = [] {
        std::multimap<Identifier, LazyString> features;
        FilterSortBufferOutput output = FilterSortBuffer(FilterSortBufferInput{
            DeleteNotification::Never(), LazyString{L"ls"},
            LineSequence::ForTests({L"value:\"ls\\n\""}), features});
        CHECK_EQ(output.lines.size(), 0ul);
      }}});
}  // namespace
}  // namespace afc::editor