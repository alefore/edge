#include "src/line_prompt_mode.h"

#include <glog/logging.h>

#include <limits>
#include <memory>
#include <ranges>
#include <string>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/file_link_mode.h"
#include "src/futures/delete_notification.h"
#include "src/infrastructure/dirname.h"
#include "src/insert_mode.h"
#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/math/naive_bayes.h"
#include "src/predictor.h"
#include "src/terminal.h"
#include "src/tests/tests.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm/escape.h"
#include "src/vm/value.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::concurrent::ChannelAll;
using afc::concurrent::VersionPropertyKey;
using afc::concurrent::VersionPropertyReceiver;
using afc::concurrent::WorkQueueScheduler;
using afc::futures::DeleteNotification;
using afc::futures::JoinValues;
using afc::futures::ListenableValue;
using afc::infrastructure::AddSeconds;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::Now;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::InsertOrDie;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::EmptyString;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::lazy_string::Token;
using afc::language::lazy_string::TokenizeBySpaces;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor {
namespace {
std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
GetCurrentFeatures(EditorState& editor) {
  std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
      output;
  for (OpenBuffer& buffer :
       *editor.buffers() | std::views::values | gc::view::Value)
    if (buffer.Read(buffer_variables::show_in_buffers_list) &&
        editor.buffer_tree().GetBufferIndex(buffer).has_value())
      output.insert(
          {L"name", NewLazyString(buffer.Read(buffer_variables::name))});
  editor.ForEachActiveBuffer([&output](OpenBuffer& buffer) {
    output.insert(
        {L"active", NewLazyString(buffer.Read(buffer_variables::name))});
    return futures::Past(EmptyValue());
  });
  return output;
}

// Generates additional features that are derived from the features returned by
// GetCurrentFeatures (and thus don't need to be saved).
std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
GetSyntheticFeatures(
    const std::unordered_multimap<
        std::wstring, NonNull<std::shared_ptr<LazyString>>>& input) {
  std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
      output;
  std::unordered_set<Path> directories;
  std::unordered_set<std::wstring> extensions;
  VLOG(5) << "Generating features from input: " << input.size();
  for (const auto& [name, value] : input) {
    if (name == L"name") {
      std::visit(
          overload{IgnoreErrors{},
                   [&](const Path& path) {
                     std::visit(
                         overload{IgnoreErrors{},
                                  [&](Path directory) {
                                    if (directory != Path::LocalDirectory())
                                      directories.insert(directory);
                                  }},
                         path.Dirname());
                     VisitOptional(
                         [&](std::wstring extension) {
                           extensions.insert(extension);
                         },
                         [] {}, path.extension());
                   }},
          Path::FromString(value));
    }
  }

  VLOG(5) << "Generating features from directories.";
  for (auto& dir : directories)
    output.insert({L"directory", NewLazyString(dir.read())});

  VLOG(5) << "Generating features from extensions.";
  for (const std::wstring& extension : extensions)
    output.insert({L"extension", NewLazyString(extension)});

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
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                input;
            input.insert({L"name", NewLazyString(L"foo.cc")});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(L"extension"), 1ul);
            CHECK(output.find(L"extension")->second->ToString() == L"cc");
          }},
     {.name = L"ExtensionsLongDirectory",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                input;
            input.insert(
                {L"name", NewLazyString(L"/home/alejo/src/edge/foo.cc")});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(L"extension"), 1ul);
            CHECK(output.find(L"extension")->second->ToString() == L"cc");
          }},
     {.name = L"ExtensionsMultiple",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                input;
            input.insert({L"name", NewLazyString(L"/home/alejo/foo.cc")});
            input.insert({L"name", NewLazyString(L"bar.cc")});
            input.insert({L"name",

                          NewLazyString(L"/home/alejo/buffer.h")});
            input.insert({L"name",

                          NewLazyString(L"/home/alejo/README.md")});
            auto output = GetSyntheticFeatures(input);
            auto range = output.equal_range(L"extension");
            CHECK_EQ(std::distance(range.first, range.second), 3l);
            while (range.first != range.second) {
              auto value = range.first->second->ToString();
              CHECK(value == L"cc" || value == L"h" || value == L"md");
              ++range.first;
            }
          }},
     {.name = L"DirectoryPlain",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                input;
            input.insert({L"name", NewLazyString(L"foo.cc")});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(L"directory"), 0ul);
          }},
     {.name = L"DirectoryPath",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                input;
            input.insert({L"name", NewLazyString(L"/home/alejo/edge/foo.cc")});
            auto output = GetSyntheticFeatures(input);
            CHECK_EQ(output.count(L"directory"), 1ul);
            CHECK(output.find(L"directory")->second->ToString() ==
                  L"/home/alejo/edge");
          }},
     {.name = L"DirectoryMultiple", .callback = [] {
        std::unordered_multimap<std::wstring,
                                NonNull<std::shared_ptr<LazyString>>>
            input;
        input.insert({L"name", NewLazyString(L"/home/alejo/edge/foo.cc")});
        input.insert({L"name", NewLazyString(L"/home/alejo/edge/bar.cc")});
        input.insert({L"name", NewLazyString(L"/home/alejo/btc/input.txt")});
        auto output = GetSyntheticFeatures(input);
        auto range = output.equal_range(L"directory");
        CHECK_EQ(std::distance(range.first, range.second), 2l);
        while (range.first != range.second) {
          auto value = range.first->second->ToString();
          CHECK(value == L"/home/alejo/edge" || value == L"/home/alejo/btc");
          ++range.first;
        }
      }}});

futures::Value<gc::Root<OpenBuffer>> GetHistoryBuffer(EditorState& editor_state,
                                                      const HistoryFile& name) {
  BufferName buffer_name(L"- history: " + name.read());
  if (auto it = editor_state.buffers()->find(buffer_name);
      it != editor_state.buffers()->end()) {
    return futures::Past(it->second);
  }
  return OpenOrCreateFile(
             {.editor_state = editor_state,
              .name = buffer_name,
              .path = editor_state.edge_path().empty()
                          ? std::nullopt
                          : std::make_optional(
                                Path::Join(editor_state.edge_path().front(),
                                           ValueOrDie(PathComponent::FromString(
                                               name.read() + L"_history")))),
              .insertion_type = BuffersList::AddBufferType::kIgnore})
      .Transform([&editor_state](gc::Root<OpenBuffer> buffer_root) {
        OpenBuffer& buffer = buffer_root.ptr().value();
        buffer.Set(buffer_variables::save_on_close, true);
        buffer.Set(buffer_variables::trigger_reload_on_buffer_write, false);
        buffer.Set(buffer_variables::show_in_buffers_list, false);
        buffer.Set(buffer_variables::atomic_lines, true);
        buffer.Set(buffer_variables::close_after_idle_seconds, 20.0);
        buffer.Set(buffer_variables::vm_lines_evaluation, false);
        if (!editor_state.has_current_buffer()) {
          // Seems lame, but what can we do?
          editor_state.set_current_buffer(buffer_root,
                                          CommandArgumentModeApplyMode::kFinal);
        }
        return buffer_root;
      });
}

ValueOrError<
    std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>>
ParseHistoryLine(const NonNull<std::shared_ptr<LazyString>>& line) {
  std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
      output;
  for (const Token& token : TokenizeBySpaces(line.value())) {
    auto colon = token.value.find(':');
    if (colon == std::wstring::npos)
      return Error(L"Unable to parse prompt line (no colon found in token): " +
                   line->ToString());
    ColumnNumber value_start = token.begin + ColumnNumberDelta(colon);
    ++value_start;  // Skip the colon.
    ColumnNumber value_end = token.end;
    if (value_end <= value_start + ColumnNumberDelta(1) ||
        line->get(value_start) != '\"' ||
        line->get(value_end.previous()) != '\"') {
      return Error(L"Unable to parse prompt line (expected quote): " +
                   line->ToString());
    }
    // Skip quotes:
    ++value_start;
    --value_end;
    output.insert({token.value.substr(0, colon),
                   Substring(line, value_start, value_end - value_start)});
  }

  std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
      synthetic_features = GetSyntheticFeatures(output);
  output.insert(synthetic_features.begin(), synthetic_features.end());

  return output;
}

auto parse_history_line_tests_registration = tests::Register(
    L"ParseHistoryLine",
    {{.name = L"BadQuote",
      .callback =
          [] {
            CHECK(IsError(ParseHistoryLine(NewLazyString(L"prompt:\""))));
          }},
     {.name = L"Empty", .callback = [] {
        auto result =
            ValueOrDie(ParseHistoryLine(NewLazyString(L"prompt:\"\"")));
        CHECK(result.find(L"prompt")->second->ToString() == L"");
      }}});

// TODO(easy, 2022-06-03): Get rid of this? Just call EscapedString directly?
NonNull<std::shared_ptr<LazyString>> QuoteString(
    NonNull<std::shared_ptr<LazyString>> src) {
  return NewLazyString(vm::EscapedString::FromString(src).CppRepresentation());
}

auto quote_string_tests_registration = tests::Register(L"QuoteString", [] {
  auto test = [](std::wstring name, std::wstring input,
                 std::wstring expected_output) {
    return tests::Test({.name = name, .callback = [=] {
                          CHECK(QuoteString(NewLazyString(input))->ToString() ==
                                expected_output);
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

NonNull<std::shared_ptr<LazyString>> BuildHistoryLine(
    EditorState& editor, NonNull<std::shared_ptr<LazyString>> input) {
  std::vector<NonNull<std::shared_ptr<LazyString>>> line_for_history;
  line_for_history.emplace_back(NewLazyString(L"prompt:"));
  line_for_history.emplace_back(QuoteString(std::move(input)));
  for (auto& [name, feature] : GetCurrentFeatures(editor)) {
    line_for_history.emplace_back(NewLazyString(L" " + name + L":"));
    line_for_history.emplace_back(QuoteString(feature));
  }
  return Concatenate(std::move(line_for_history));
}

NonNull<std::shared_ptr<Line>> ColorizeLine(
    NonNull<std::shared_ptr<LazyString>> line,
    std::vector<TokenAndModifiers> tokens) {
  sort(tokens.begin(), tokens.end(),
       [](const TokenAndModifiers& a, const TokenAndModifiers& b) {
         return a.token.begin < b.token.begin;
       });
  VLOG(6) << "Producing output: " << line->ToString();
  LineBuilder options;
  ColumnNumber position;
  auto push_to_position = [&](ColumnNumber end, LineModifierSet modifiers) {
    if (end <= position) return;
    VLOG(8) << "Adding substring with modifiers: " << position << ", "
            << modifiers;
    options.AppendString(Substring(line, position, end - position),
                         std::move(modifiers));
    position = end;
  };
  for (const auto& t : tokens) {
    push_to_position(t.token.begin, {});
    push_to_position(t.token.end, t.modifiers);
  }
  push_to_position(ColumnNumber() + line->size(), {});
  return MakeNonNullShared<Line>(std::move(options).Build());
}

struct FilterSortHistorySyncOutput {
  std::vector<Error> errors;
  std::vector<NonNull<std::shared_ptr<const Line>>> lines;
};

FilterSortHistorySyncOutput FilterSortHistorySync(
    DeleteNotification::Value abort_value, std::wstring filter,
    LineSequence history_contents,
    std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
        features) {
  FilterSortHistorySyncOutput output;
  if (abort_value.has_value()) return output;
  // Sets of features for each unique `prompt` value in the history.
  math::naive_bayes::History history_data;
  // Tokens by parsing the `prompt` value in the history.
  std::unordered_map<math::naive_bayes::Event, std::vector<Token>>
      history_prompt_tokens;
  std::vector<Token> filter_tokens =
      TokenizeBySpaces(NewLazyString(filter).value());
  history_contents.EveryLine(
      [&](LineNumber, const NonNull<std::shared_ptr<const Line>>& line) {
        VLOG(8) << "Considering line: " << line->ToString();
        auto warn_if = [&](bool condition, Error error) {
          if (condition) {
            // We don't use AugmentError because we'd rather append to the
            // end of the description, not the beginning.
            error = Error(error.read() + L": " + line->contents()->ToString());
            VLOG(5) << "Found error: " << error;
            output.errors.push_back(error);
          }
          return condition;
        };
        if (line->empty()) return true;
        ValueOrError<std::unordered_multimap<
            std::wstring, NonNull<std::shared_ptr<LazyString>>>>
            line_keys_or_error = ParseHistoryLine(line->contents());
        auto* line_keys = std::get_if<0>(&line_keys_or_error);
        if (line_keys == nullptr) {
          output.errors.push_back(std::get<Error>(line_keys_or_error));
          return !abort_value.has_value();
        }
        auto range = line_keys->equal_range(L"prompt");
        int prompt_count = std::distance(range.first, range.second);
        if (warn_if(prompt_count == 0,
                    Error(L"Line is missing `prompt` section")) ||
            warn_if(prompt_count != 1,
                    Error(L"Line has multiple `prompt` sections"))) {
          return !abort_value.has_value();
        }

        std::visit(
            overload{
                [range](Error error) {
                  LOG(INFO) << AugmentError(
                      L"Unescaping string: " + range.first->second->ToString(),
                      error);
                },
                [&](vm::EscapedString cpp_string) {
                  VLOG(8) << "Considering history value: "
                          << cpp_string.EscapedRepresentation();
                  NonNull<std::shared_ptr<LazyString>> prompt_value =
                      cpp_string.OriginalString();
                  if (FindFirstColumnWithPredicate(
                          prompt_value.value(),
                          [](ColumnNumber, wchar_t c) { return c == L'\n'; })
                          .has_value()) {
                    VLOG(5)
                        << "Ignoring value that contains a new line character.";
                    return;
                  }
                  std::vector<Token> line_tokens = ExtendTokensToEndOfString(
                      prompt_value,
                      TokenizeNameForPrefixSearches(prompt_value));
                  // TODO(easy, 2022-11-26): Get rid of call ToString.
                  math::naive_bayes::Event event_key(
                      cpp_string.OriginalString()->ToString());
                  std::vector<math::naive_bayes::FeaturesSet>* features_output =
                      nullptr;
                  if (filter_tokens.empty()) {
                    VLOG(6) << "Accepting value (empty filters): "
                            << line->contents().value();
                    features_output = &history_data[event_key];
                  } else if (auto match = FindFilterPositions(filter_tokens,
                                                              line_tokens);
                             match.has_value()) {
                    VLOG(5) << "Accepting value, produced a match: "
                            << line->contents().value();
                    features_output = &history_data[event_key];
                    history_prompt_tokens.insert(
                        {event_key, std::move(match.value())});
                  } else {
                    VLOG(6) << "Ignoring value, no match: "
                            << line->contents().value();
                    return;
                  }
                  math::naive_bayes::FeaturesSet current_features;
                  for (auto& [key, value] : *line_keys) {
                    if (key != L"prompt") {
                      current_features.insert(math::naive_bayes::Feature(
                          key + L":" + QuoteString(value)->ToString()));
                    }
                  }
                  features_output->push_back(std::move(current_features));
                }},
            vm::EscapedString::Parse(range.first->second));
        return !abort_value.has_value();
      });

  VLOG(4) << "Matches found: " << history_data.read().size();

  // For sorting.
  math::naive_bayes::FeaturesSet current_features;
  for (const auto& [name, value] : features) {
    current_features.insert(math::naive_bayes::Feature(
        name + L":" + QuoteString(value)->ToString()));
  }
  for (const auto& [name, value] : GetSyntheticFeatures(features)) {
    current_features.insert(math::naive_bayes::Feature(
        name + L":" + QuoteString(value)->ToString()));
  }

  for (math::naive_bayes::Event& key :
       math::naive_bayes::Sort(history_data, current_features)) {
    output.lines.push_back(
        ColorizeLine(NewLazyString(key.read()),
                     container::MaterializeVector(
                         history_prompt_tokens[key] |
                         std::views::transform([](const Token& token) {
                           VLOG(6) << "Add token BOLD: " << token;
                           return TokenAndModifiers{
                               token, LineModifierSet{LineModifier::kBold}};
                         }))));
  }
  return output;
}

auto filter_sort_history_sync_tests_registration = tests::Register(
    L"FilterSortHistorySync",
    {{.name = L"EmptyFilter",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            FilterSortHistorySyncOutput output = FilterSortHistorySync(
                DeleteNotification::Never(), L"", LineSequence(), features);
            CHECK(output.lines.empty());
          }},
     {.name = L"NoMatch",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            FilterSortHistorySyncOutput output = FilterSortHistorySync(
                DeleteNotification::Never(), L"quux",
                LineSequence::ForTests(
                    {L"prompt:\"foobar\"", L"prompt:\"foo\""}),
                features);
            CHECK(output.lines.empty());
          }},
     {.name = L"MatchAfterEscape",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            FilterSortHistorySyncOutput output = FilterSortHistorySync(
                DeleteNotification::Never(), L"nbar",
                LineSequence::ForTests({L"prompt:\"foo\\\\nbardo\""}),
                features);
            CHECK_EQ(output.lines.size(), 1ul);
            const Line& line = output.lines[0].value();
            CHECK(line.ToString() == L"foo\\nbardo");

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
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            FilterSortHistorySyncOutput output = FilterSortHistorySync(
                DeleteNotification::Never(), L"nbar",
                LineSequence::ForTests({L"prompt:\"foo\\nbar\""}), features);
            CHECK(output.lines.empty());
          }},
     {.name = L"IgnoresInvalidEntries",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            FilterSortHistorySyncOutput output = FilterSortHistorySync(
                DeleteNotification::Never(), L"f",
                LineSequence::ForTests(
                    {L"prompt:\"foobar \\\"", L"prompt:\"foo\"",
                     L"prompt:\"foo\n bar\"", L"prompt:\"foo \\o bar \\\""}),
                features);
            CHECK_EQ(output.lines.size(), 1ul);
            CHECK(output.lines[0]->ToString() == L"foo");
          }},
     {.name = L"HistoryWithRawNewLine",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            FilterSortHistorySyncOutput output = FilterSortHistorySync(
                DeleteNotification::Never(), L"ls",
                LineSequence::ForTests({L"prompt:\"ls\n\""}), features);
            CHECK(output.lines.empty());
          }},
     {.name = L"HistoryWithEscapedNewLine", .callback = [] {
        std::unordered_multimap<std::wstring,
                                NonNull<std::shared_ptr<LazyString>>>
            features;
        FilterSortHistorySyncOutput output = FilterSortHistorySync(
            DeleteNotification::Never(), L"ls",
            LineSequence::ForTests({L"prompt:\"ls\\n\""}), features);
        CHECK_EQ(output.lines.size(), 0ul);
      }}});

futures::Value<gc::Root<OpenBuffer>> FilterHistory(
    EditorState& editor_state, gc::Root<OpenBuffer> history_buffer,
    DeleteNotification::Value abort_value, std::wstring filter) {
  BufferName name(L"- history filter: " + history_buffer.ptr()->name().read() +
                  L": " + filter);
  gc::Root<OpenBuffer> filter_buffer_root =
      OpenBuffer::New({.editor = editor_state, .name = name});
  OpenBuffer& filter_buffer = filter_buffer_root.ptr().value();
  filter_buffer.Set(buffer_variables::allow_dirty_delete, true);
  filter_buffer.Set(buffer_variables::show_in_buffers_list, false);
  filter_buffer.Set(buffer_variables::delete_into_paste_buffer, false);
  filter_buffer.Set(buffer_variables::atomic_lines, true);
  filter_buffer.Set(buffer_variables::line_width, 1);

  return history_buffer.ptr()
      ->WaitForEndOfFile()
      .Transform([&editor_state, filter_buffer_root, history_buffer,
                  abort_value, filter](EmptyValue) {
        return editor_state.thread_pool().Run(
            std::bind_front(FilterSortHistorySync, abort_value, filter,
                            history_buffer.ptr()->contents().snapshot(),
                            GetCurrentFeatures(editor_state)));
      })
      .Transform([&editor_state, abort_value, filter_buffer_root,
                  &filter_buffer](FilterSortHistorySyncOutput output) {
        LOG(INFO) << "Receiving output from history evaluator.";
        if (!output.errors.empty()) {
          editor_state.work_queue()->DeleteLater(
              AddSeconds(Now(), 1.0),
              editor_state.status().SetExpiringInformationText(
                  MakeNonNullShared<Line>(
                      LineBuilder(NewLazyString(output.errors.front().read()))
                          .Build())));
        }
        if (!abort_value.has_value()) {
          filter_buffer.AppendLines(std::move(output.lines));
          if (filter_buffer.lines_size() > LineNumberDelta(1)) {
            VLOG(5) << "Erasing the first (empty) line.";
            CHECK(filter_buffer.LineAt(LineNumber())->empty());
            filter_buffer.EraseLines(LineNumber(), LineNumber().next());
          }
        }
        return filter_buffer_root;
      });
}

class StatusVersionAdapter;

// Holds the state required to show and update a prompt.
class PromptState : public std::enable_shared_from_this<PromptState> {
  struct ConstructorAccessKey {};

 public:
  static NonNull<std::shared_ptr<PromptState>> New(
      PromptOptions options, gc::Root<OpenBuffer> history) {
    return MakeNonNullShared<PromptState>(
        std::move(options), std::move(history), ConstructorAccessKey());
  }

  PromptState(PromptOptions options, gc::Root<OpenBuffer> history,
              ConstructorAccessKey)
      : options_(std::move(options)),
        history_(std::move(history)),
        prompt_buffer_(GetPromptBuffer()),
        status_buffer_([&]() -> std::optional<gc::Root<OpenBuffer>> {
          if (options.status == PromptOptions::Status::kEditor)
            return std::nullopt;
          auto active_buffers = editor_state().active_buffers();
          return active_buffers.size() == 1
                     ? active_buffers[0]
                     : std::optional<gc::Root<OpenBuffer>>();
        }()),
        status_(status_buffer_.has_value() ? status_buffer_->ptr()->status()
                                           : editor_state().status()),
        original_modifiers_(editor_state().modifiers()) {
    editor_state().set_modifiers(Modifiers());
  }

  InsertModeOptions insert_mode_options();
  const PromptOptions& options() const { return options_; }
  const gc::Root<OpenBuffer>& history() const { return history_; }
  const gc::Root<OpenBuffer>& prompt_buffer() const { return prompt_buffer_; }
  EditorState& editor_state() const { return options_.editor_state; }

  // The prompt has disappeared.
  bool IsGone() const { return status().GetType() != Status::Type::kPrompt; }

  Status& status() const { return status_; }

  futures::Value<EmptyValue> OnModify();

 private:
  void Reset() {
    status().Reset();
    editor_state().set_modifiers(original_modifiers_);
  }

  gc::Root<OpenBuffer> GetPromptBuffer() const {
    BufferName name(L"- prompt");
    if (auto it = options_.editor_state.buffers()->find(name);
        it != options_.editor_state.buffers()->end()) {
      gc::Root<OpenBuffer> buffer_root = it->second;
      OpenBuffer& buffer = buffer_root.ptr().value();
      buffer.ClearContents(MutableLineSequence::ObserverBehavior::kShow);
      CHECK_EQ(buffer.EndLine(), LineNumber(0));
      CHECK(buffer.contents().back()->empty());
      buffer.Set(buffer_variables::contents_type,
                 options_.prompt_contents_type);
      buffer.Reload();
      InitializePromptBuffer(buffer);
      return buffer_root;
    }
    gc::Root<OpenBuffer> buffer_root =
        OpenBuffer::New({.editor = options_.editor_state, .name = name});
    OpenBuffer& buffer = buffer_root.ptr().value();
    buffer.Set(buffer_variables::allow_dirty_delete, true);
    buffer.Set(buffer_variables::show_in_buffers_list, false);
    buffer.Set(buffer_variables::delete_into_paste_buffer, false);
    buffer.Set(buffer_variables::save_on_close, false);
    buffer.Set(buffer_variables::persist_state, false);
    buffer.Set(buffer_variables::completion_model_paths, L"");
    InsertOrDie(*options_.editor_state.buffers(),
                {name, buffer_root.ptr().ToRoot()});
    InitializePromptBuffer(buffer);
    return buffer_root;
  }

  void InitializePromptBuffer(OpenBuffer& buffer) const {
    buffer.Set(buffer_variables::contents_type, options_.prompt_contents_type);
    buffer.ApplyToCursors(transformation::Insert(
        {.contents_to_insert = LineSequence::WithLine(
             MakeNonNullShared<Line>(options_.initial_value))}));
  }

  // status_buffer is the buffer with the contents of the prompt. tokens_future
  // is received as a future so that we can detect if the prompt input changes
  // between the time when `ColorizePrompt` is executed and the time when the
  // tokens become available.
  void ColorizePrompt(DeleteNotification::Value abort_value,
                      const NonNull<std::shared_ptr<const Line>>& original_line,
                      ColorizePromptOptions options) {
    CHECK_EQ(prompt_buffer_.ptr()->lines_size(), LineNumberDelta(1));
    if (IsGone()) {
      LOG(INFO) << "Status is no longer a prompt, aborting colorize prompt.";
      return;
    }

    if (status().prompt_buffer().has_value() &&
        &status().prompt_buffer()->ptr().value() !=
            &prompt_buffer_.ptr().value()) {
      LOG(INFO) << "Prompt buffer has changed, aborting colorize prompt.";
      return;
    }
    if (abort_value.has_value()) {
      LOG(INFO) << "Abort notification notified, aborting colorize prompt.";
      return;
    }

    CHECK_EQ(prompt_buffer_.ptr()->lines_size(), LineNumberDelta(1));
    auto line = prompt_buffer_.ptr()->LineAt(LineNumber(0));
    if (original_line->ToString() != line->ToString()) {
      LOG(INFO) << "Line has changed, ignoring prompt colorize update.";
      return;
    }

    prompt_buffer_.ptr()->AppendRawLine(
        ColorizeLine(line->contents(), std::move(options.tokens)));
    prompt_buffer_.ptr()->EraseLines(LineNumber(0), LineNumber(1));
    std::visit(overload{[](ColorizePromptOptions::ContextUnmodified) {},
                        [&](ColorizePromptOptions::ContextClear) {
                          status().set_context(std::nullopt);
                        },
                        [&](const ColorizePromptOptions::ContextBuffer& value) {
                          status().set_context(value.buffer);
                        }},
               options.context);
  }

  const PromptOptions options_;
  const gc::Root<OpenBuffer> history_;

  // The buffer we create to hold the prompt.
  const gc::Root<OpenBuffer> prompt_buffer_;

  // If the status is associated with a buffer, we capture it here; that allows
  // us to ensure that the status won't be deallocated under our feet (when the
  // buffer is ephemeral).
  const std::optional<gc::Root<OpenBuffer>> status_buffer_;
  Status& status_;
  const Modifiers original_modifiers_;

  // Notification that can be used by a StatusVersionAdapter (and its customers)
  // to detect that the corresponding version is stale.
  NonNull<std::shared_ptr<DeleteNotification>> abort_notification_;
};

// Allows asynchronous operations to augment the information displayed in the
// status. Uses abort_notification_value to detect when the version is stale.
class StatusVersionAdapter {
 public:
  StatusVersionAdapter(NonNull<std::shared_ptr<PromptState>> prompt_state)
      : prompt_state_(std::move(prompt_state)),
        status_version_(prompt_state_->status()
                            .prompt_extra_information()
                            ->StartNewVersion()) {}

  // The prompt has disappeared.
  bool Expired() const {
    return prompt_state_->IsGone() || status_version_->IsExpired();
  }

  template <typename T>
  void SetStatusValue(VersionPropertyKey key, T value) {
    CHECK(prompt_state_->status().GetType() == Status::Type::kPrompt);
    status_version_->SetValue(key, value);
  }

  template <typename T>
  void SetStatusValues(T container) {
    if (!Expired())
      for (const auto& [key, value] : container) SetStatusValue(key, value);
  }

 private:
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;

  const NonNull<std::unique_ptr<VersionPropertyReceiver::Version>>
      status_version_;
};

futures::Value<EmptyValue> PromptState::OnModify() {
  if (options().colorize_options_provider == nullptr ||
      status().GetType() != Status::Type::kPrompt)
    return futures::Past(EmptyValue());

  NonNull<std::shared_ptr<const Line>> line =
      prompt_buffer_.ptr()->contents().at(LineNumber());

  auto status_value_viewer = MakeNonNullShared<StatusVersionAdapter>(
      NonNull<std::shared_ptr<PromptState>>::Unsafe(shared_from_this()));

  abort_notification_ = MakeNonNullShared<DeleteNotification>();
  auto abort_notification_value = abort_notification_->listenable_value();

  NonNull<std::unique_ptr<ProgressChannel>> progress_channel =
      MakeNonNullUnique<ChannelAll<ProgressInformation>>(
          [work_queue = prompt_buffer_.ptr()->work_queue(),
           status_value_viewer](ProgressInformation extra_information) {
            work_queue->Schedule({.callback = [status_value_viewer,
                                               extra_information] {
              if (!status_value_viewer->Expired()) {
                status_value_viewer->SetStatusValues(extra_information.values);
                status_value_viewer->SetStatusValues(
                    extra_information.counters);
              }
            }});
          });

  return JoinValues(
             FilterHistory(editor_state(), history(), abort_notification_value,
                           line->ToString())
                 .Transform([status_value_viewer](
                                gc::Root<OpenBuffer> filtered_history) {
                   LOG(INFO) << "Propagating history information to status.";
                   if (!status_value_viewer->Expired()) {
                     bool last_line_empty =
                         filtered_history.ptr()
                             ->LineAt(filtered_history.ptr()->EndLine())
                             ->empty();
                     status_value_viewer->SetStatusValue(
                         VersionPropertyKey(L"history"),
                         filtered_history.ptr()->lines_size().read() -
                             (last_line_empty ? 1 : 0));
                   }
                   return EmptyValue();
                 }),
             options()
                 .colorize_options_provider(line->contents(),
                                            std::move(progress_channel),
                                            abort_notification_value)
                 .Transform([shared_this = shared_from_this(),
                             abort_notification_value, line](
                                ColorizePromptOptions colorize_prompt_options) {
                   LOG(INFO) << "Calling ColorizePrompt with results.";
                   shared_this->ColorizePrompt(abort_notification_value, line,
                                               colorize_prompt_options);
                   return EmptyValue();
                 }))
      .Transform([](auto) { return EmptyValue(); });
}

class HistoryScrollBehavior : public ScrollBehavior {
 public:
  HistoryScrollBehavior(
      futures::ListenableValue<gc::Root<OpenBuffer>> filtered_history,
      NonNull<std::shared_ptr<const Line>> original_input,
      NonNull<std::shared_ptr<PromptState>> prompt_state)
      : filtered_history_(std::move(filtered_history)),
        original_input_(std::move(original_input)),
        prompt_state_(std::move(prompt_state)),
        previous_context_(prompt_state_->status().context()) {
    CHECK(prompt_state_->status().GetType() == Status::Type::kPrompt ||
          prompt_state_->IsGone());
  }

  void PageUp(OpenBuffer& buffer) override {
    ScrollHistory(buffer, LineNumberDelta(-1));
  }

  void PageDown(OpenBuffer& buffer) override {
    ScrollHistory(buffer, LineNumberDelta(+1));
  }

  void Up(OpenBuffer& buffer) override {
    ScrollHistory(buffer, LineNumberDelta(-1));
  }

  void Down(OpenBuffer& buffer) override {
    ScrollHistory(buffer, LineNumberDelta(+1));
  }

  void Left(OpenBuffer& buffer) override {
    DefaultScrollBehavior().Left(buffer);
  }

  void Right(OpenBuffer& buffer) override {
    DefaultScrollBehavior().Right(buffer);
  }

  void Begin(OpenBuffer& buffer) override {
    DefaultScrollBehavior().Begin(buffer);
  }

  void End(OpenBuffer& buffer) override { DefaultScrollBehavior().End(buffer); }

 private:
  void ScrollHistory(OpenBuffer& buffer, LineNumberDelta delta) const {
    if (prompt_state_->IsGone()) return;
    if (delta > LineNumberDelta() && !filtered_history_.has_value()) {
      ReplaceContents(buffer, LineSequence());
      return;
    }
    filtered_history_.AddListener([delta, buffer_root = buffer.NewRoot(),
                                   &buffer, original_input = original_input_,
                                   prompt_state = prompt_state_,
                                   previous_context = previous_context_](
                                      gc::Root<OpenBuffer> history_root) {
      std::shared_ptr<const Line> line_to_insert;
      OpenBuffer& history = history_root.ptr().value();
      if (history.contents().size() > LineNumberDelta(1) ||
          !history.LineAt(LineNumber())->empty()) {
        LineColumn position = history.position();
        position.line = std::min(position.line.PlusHandlingOverflow(delta),
                                 LineNumber() + history.contents().size());
        history.set_position(position);
        if (position.line < LineNumber(0) + history.contents().size()) {
          prompt_state->status().set_context(history_root);
          VisitPointer(
              history.CurrentLineOrNull(),
              [&line_to_insert](NonNull<std::shared_ptr<const Line>> line) {
                line_to_insert = line.get_shared();
              },
              [] {});
        } else if (prompt_state->status().context() != previous_context) {
          prompt_state->status().set_context(previous_context);
          line_to_insert = original_input.get_shared();
        }
      }
      LineBuilder line_builder;
      VisitPointer(
          line_to_insert,
          [&](NonNull<std::shared_ptr<const Line>> line) {
            VLOG(5) << "Inserting line: " << line->ToString();
            line_builder.Append(LineBuilder(line.value()));
          },
          [] {});
      ReplaceContents(buffer, LineSequence::WithLine(MakeNonNullShared<Line>(
                                  std::move(line_builder).Build())));
    });
  }

  static void ReplaceContents(OpenBuffer& buffer,
                              LineSequence contents_to_insert) {
    buffer.ApplyToCursors(transformation::Delete{
        .modifiers = {.structure = Structure::kLine,
                      .paste_buffer_behavior =
                          Modifiers::PasteBufferBehavior::kDoNothing,
                      .boundary_begin = Modifiers::LIMIT_CURRENT,
                      .boundary_end = Modifiers::LIMIT_CURRENT},
        .initiator = transformation::Delete::Initiator::kInternal});

    buffer.ApplyToCursors(transformation::Insert{
        .contents_to_insert = std::move(contents_to_insert)});
  }

  const futures::ListenableValue<gc::Root<OpenBuffer>> filtered_history_;
  const NonNull<std::shared_ptr<const Line>> original_input_;
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;
  const std::optional<gc::Root<OpenBuffer>> previous_context_;
};

class HistoryScrollBehaviorFactory : public ScrollBehaviorFactory {
 public:
  HistoryScrollBehaviorFactory(
      NonNull<std::shared_ptr<PromptState>> prompt_state)
      : prompt_state_(std::move(prompt_state)) {}

  futures::Value<NonNull<std::unique_ptr<ScrollBehavior>>> Build(
      DeleteNotification::Value abort_value) override {
    OpenBuffer& buffer = prompt_state_->prompt_buffer().ptr().value();
    CHECK_GT(buffer.lines_size(), LineNumberDelta(0));
    NonNull<std::shared_ptr<const Line>> input =
        buffer.contents().at(LineNumber(0));
    return futures::Past(MakeNonNullUnique<HistoryScrollBehavior>(
        futures::ListenableValue(
            FilterHistory(prompt_state_->editor_state(),
                          prompt_state_->history(), abort_value,
                          input->ToString())
                .Transform([](gc::Root<OpenBuffer> history_filtered) {
                  history_filtered.ptr()->set_current_position_line(
                      LineNumber(0) +
                      history_filtered.ptr()->contents().size());
                  return history_filtered;
                })),
        input, prompt_state_));
  }

 private:
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;
};

class LinePromptCommand : public Command {
  struct ConstructorAccessTag {};

  EditorState& editor_state_;
  const std::wstring description_;
  const std::function<PromptOptions()> options_supplier_;

 public:
  static gc::Root<LinePromptCommand> New(
      EditorState& editor_state, std::wstring description,
      std::function<PromptOptions()> options_supplier) {
    return editor_state.gc_pool().NewRoot(MakeNonNullUnique<LinePromptCommand>(
        ConstructorAccessTag(), editor_state, std::move(description),
        std::move(options_supplier)));
  }

  LinePromptCommand(ConstructorAccessTag, EditorState& editor_state,
                    std::wstring description,
                    std::function<PromptOptions()> options_supplier)
      : editor_state_(editor_state),
        description_(std::move(description)),
        options_supplier_(std::move(options_supplier)) {}

  std::wstring Description() const override { return description_; }
  std::wstring Category() const override { return L"Prompt"; }

  void ProcessInput(ExtendedChar) override {
    auto buffer = editor_state_.current_buffer();
    if (!buffer.has_value()) return;
    auto options = options_supplier_();
    if (editor_state_.structure() == Structure::kLine) {
      editor_state_.ResetStructure();
      VisitPointer(
          buffer->ptr()->CurrentLineOrNull(),
          [&](NonNull<std::shared_ptr<const Line>> line) {
            AddLineToHistory(editor_state_, options.history_file,
                             line->contents());
            options.handler(line->contents());
          },
          [] {});
    } else {
      Prompt(std::move(options));
    }
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }
};

}  // namespace

HistoryFile HistoryFileFiles() { return HistoryFile(L"files"); }
HistoryFile HistoryFileCommands() { return HistoryFile(L"commands"); }

// input must not be escaped.
void AddLineToHistory(EditorState& editor, const HistoryFile& history_file,
                      NonNull<std::shared_ptr<LazyString>> input) {
  if (input->size().IsZero()) return;
  switch (editor.args().prompt_history_behavior) {
    case CommandLineValues::HistoryFileBehavior::kUpdate:
      GetHistoryBuffer(editor, history_file)
          .Transform([history_line = BuildHistoryLine(editor, input)](
                         gc::Root<OpenBuffer> history) {
            history.ptr()->AppendLine(history_line);
            return Success();
          });
      return;
    case CommandLineValues::HistoryFileBehavior::kReadOnly:
      break;
  }
}

InsertModeOptions PromptState::insert_mode_options() {
  NonNull<std::shared_ptr<PromptState>> prompt_state =
      NonNull<std::shared_ptr<PromptState>>::Unsafe(shared_from_this());
  return InsertModeOptions{
      .editor_state = editor_state(),
      .buffers = {{prompt_state->prompt_buffer()}},
      .modify_handler =
          [prompt_state](OpenBuffer& buffer) {
            CHECK_EQ(&buffer, &prompt_state->prompt_buffer().ptr().value());
            return prompt_state->OnModify();
          },
      .scroll_behavior =
          MakeNonNullShared<HistoryScrollBehaviorFactory>(prompt_state),
      .escape_handler =
          [prompt_state]() {
            LOG(INFO) << "Running escape_handler from Prompt.";
            prompt_state->Reset();

            if (prompt_state->options().cancel_handler) {
              VLOG(5) << "Running cancel handler.";
              prompt_state->options().cancel_handler();
            } else {
              VLOG(5) << "Running handler on empty input.";
              prompt_state->options().handler(EmptyString());
            }
            prompt_state->editor_state().set_keyboard_redirect(nullptr);
          },
      .new_line_handler =
          [prompt_state](OpenBuffer& buffer) {
            NonNull<std::shared_ptr<LazyString>> input =
                buffer.CurrentLine()->contents();
            AddLineToHistory(prompt_state->editor_state(),
                             prompt_state->options().history_file, input);
            auto ensure_survival_of_current_closure =
                prompt_state->editor_state().set_keyboard_redirect(nullptr);
            prompt_state->Reset();
            return prompt_state->options().handler(input);
          },
      .start_completion =
          [prompt_state](OpenBuffer& buffer) {
            NonNull<std::shared_ptr<LazyString>> input =
                buffer.CurrentLine()->contents();
            LOG(INFO) << "Triggering predictions from: " << input.value();
            CHECK(prompt_state->status().prompt_extra_information() != nullptr);
            Predict({.editor_state = prompt_state->editor_state(),
                     .predictor = prompt_state->options().predictor,
                     .input = buffer.NewRoot(),
                     .source_buffers = prompt_state->options().source_buffers})
                .Transform([prompt_state,
                            input](std::optional<PredictResults> results) {
                  if (!results.has_value()) return EmptyValue();
                  if (results.value().common_prefix.has_value() &&
                      !results.value().common_prefix.value().empty() &&
                      input->ToString() !=
                          results.value().common_prefix.value()) {
                    LOG(INFO) << "Prediction advanced from " << input.value()
                              << " to " << results.value();

                    prompt_state->prompt_buffer().ptr()->ApplyToCursors(
                        transformation::Delete{
                            .modifiers =
                                {.structure = Structure::kLine,
                                 .paste_buffer_behavior =
                                     Modifiers::PasteBufferBehavior::kDoNothing,
                                 .boundary_begin = Modifiers::LIMIT_CURRENT,
                                 .boundary_end = Modifiers::LIMIT_CURRENT},
                            .initiator =
                                transformation::Delete::Initiator::kInternal});

                    NonNull<std::shared_ptr<LazyString>> line =
                        NewLazyString(results.value().common_prefix.value());

                    prompt_state->prompt_buffer().ptr()->ApplyToCursors(
                        transformation::Insert(
                            {.contents_to_insert =
                                 LineSequence::WithLine(MakeNonNullShared<Line>(
                                     LineBuilder(std::move(line)).Build()))}));
                    prompt_state->OnModify();
                    return EmptyValue();
                  }
                  LOG(INFO) << "Prediction didn't advance.";
                  auto buffers = prompt_state->editor_state().buffers();
                  auto name = PredictionsBufferName();
                  if (auto it = buffers->find(name); it != buffers->end()) {
                    it->second.ptr()->set_current_position_line(LineNumber(0));
                    prompt_state->editor_state().set_current_buffer(
                        it->second, CommandArgumentModeApplyMode::kFinal);
                    if (!prompt_state->editor_state()
                             .status()
                             .prompt_buffer()
                             .has_value()) {
                      it->second.ptr()->status().CopyFrom(
                          prompt_state->status());
                    }
                  } else {
                    prompt_state->editor_state().status().InsertError(Error(
                        L"Error: Predict: predictions buffer not found: " +
                        name.read()));
                  }
                  return EmptyValue();
                });
            return true;
          }};
}

void Prompt(PromptOptions options) {
  CHECK(options.handler != nullptr);
  EditorState& editor_state = options.editor_state;
  HistoryFile history_file = options.history_file;
  GetHistoryBuffer(editor_state, history_file)
      .Transform([options = std::move(options)](gc::Root<OpenBuffer> history) {
        history.ptr()->set_current_position_line(
            LineNumber(0) + history.ptr()->contents().size());

        auto prompt_state = PromptState::New(options, history);
        EnterInsertMode(prompt_state->insert_mode_options());

        // We do this after `EnterInsertMode` because `EnterInsertMode`
        // resets the status.
        prompt_state->status().set_prompt(options.prompt,
                                          prompt_state->prompt_buffer());
        prompt_state->OnModify();
        return futures::Past(EmptyValue());
      });
}

gc::Root<Command> NewLinePromptCommand(
    EditorState& editor_state, std::wstring description,
    std::function<PromptOptions()> options_supplier) {
  return LinePromptCommand::New(editor_state, std::move(description),
                                std::move(options_supplier));
}

}  // namespace afc::editor
