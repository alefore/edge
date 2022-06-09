#include "src/line_prompt_mode.h"

#include <glog/logging.h>

#include <limits>
#include <memory>
#include <string>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/command_mode.h"
#include "src/concurrent/notification.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/insert_mode.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/naive_bayes.h"
#include "src/predictor.h"
#include "src/terminal.h"
#include "src/tests/tests.h"
#include "src/tokenize.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/vm/public/escape.h"
#include "src/vm/public/value.h"

namespace afc::editor {
using concurrent::Notification;
using concurrent::WorkQueueChannelConsumeMode;
using infrastructure::Path;
using infrastructure::PathComponent;
using language::EmptyValue;
using language::Error;
using language::IgnoreErrors;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::Success;
using language::ValueOrError;
using language::VisitPointer;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::EmptyString;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;

namespace gc = language::gc;
namespace {

std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
GetCurrentFeatures(EditorState& editor) {
  std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
      output;
  for (std::pair<BufferName, gc::Root<OpenBuffer>> entry : *editor.buffers()) {
    OpenBuffer& buffer = entry.second.ptr().value();
    if (buffer.Read(buffer_variables::show_in_buffers_list) &&
        editor.buffer_tree().GetBufferIndex(buffer).has_value()) {
      output.insert(
          {L"name", NewLazyString(buffer.Read(buffer_variables::name))});
    }
  }
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
      ValueOrError<Path> value_path = Path::FromString(value->ToString());
      Path* path = std::get_if<Path>(&value_path);
      if (path == nullptr) continue;
      std::visit(overload{IgnoreErrors{},
                          [&](Path directory) {
                            if (directory != Path::LocalDirectory())
                              directories.insert(directory);
                          }},
                 path->Dirname());
      if (std::optional<std::wstring> extension = path->extension();
          extension.has_value()) {
        extensions.insert(extension.value());
      }
    }
  }

  VLOG(5) << "Generating features from directories.";
  for (auto& dir : directories) {
    output.insert({L"directory", NewLazyString(dir.read())});
  }

  VLOG(5) << "Generating features from extensions.";
  for (auto& extension : extensions) {
    output.insert({L"extension", NewLazyString(std::move(extension))});
  }
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
    if (colon == std::wstring::npos) {
      return Error(L"Unable to parse prompt line (no colon found in token): " +
                   line->ToString());
    }
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
  for (std::pair<std::wstring, NonNull<std::shared_ptr<LazyString>>>
           additional_features : GetSyntheticFeatures(output)) {
    output.insert(additional_features);
  }
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
  return NewLazyString(
      vm::EscapedString::FromString(src->ToString()).CppRepresentation());
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
  Line::Options options;
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
  return MakeNonNullShared<Line>(std::move(options));
}

struct FilterSortHistorySyncOutput {
  std::vector<Error> errors;
  std::vector<NonNull<std::shared_ptr<Line>>> lines;
};

FilterSortHistorySyncOutput FilterSortHistorySync(
    NonNull<std::shared_ptr<Notification>> abort_notification,
    std::wstring filter,
    NonNull<std::shared_ptr<BufferContents>> history_contents,
    std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
        features) {
  FilterSortHistorySyncOutput output;
  if (abort_notification->HasBeenNotified()) return output;
  // Sets of features for each unique `prompt` value in the history.
  naive_bayes::History history_data;
  // Tokens by parsing the `prompt` value in the history.
  std::unordered_map<naive_bayes::Event, std::vector<Token>>
      history_prompt_tokens;
  std::vector<Token> filter_tokens =
      TokenizeBySpaces(NewLazyString(filter).value());
  history_contents->EveryLine([&](LineNumber, const Line& line) {
    VLOG(8) << "Considering line: " << line.ToString();
    auto warn_if = [&](bool condition, Error error) {
      if (condition) {
        // We don't use AugmentError because we'd rather append to the
        // end of the description, not the beginning.
        error = Error(error.read() + L": " + line.contents()->ToString());
        VLOG(5) << "Found error: " << error;
        output.errors.push_back(error);
      }
      return condition;
    };
    if (line.empty()) return true;
    ValueOrError<std::unordered_multimap<std::wstring,
                                         NonNull<std::shared_ptr<LazyString>>>>
        line_keys_or_error = ParseHistoryLine(line.contents());
    auto* line_keys = std::get_if<0>(&line_keys_or_error);
    if (line_keys == nullptr) {
      output.errors.push_back(std::get<Error>(line_keys_or_error));
      return !abort_notification->HasBeenNotified();
    }
    auto range = line_keys->equal_range(L"prompt");
    int prompt_count = std::distance(range.first, range.second);
    if (warn_if(prompt_count == 0,
                Error(L"Line is missing `prompt` section")) ||
        warn_if(prompt_count != 1,
                Error(L"Line has multiple `prompt` sections"))) {
      return !abort_notification->HasBeenNotified();
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
                  NewLazyString(cpp_string.OriginalString());
              if (FindFirstColumnWithPredicate(
                      prompt_value.value(),
                      [](ColumnNumber, wchar_t c) { return c == L'\n'; })
                      .has_value()) {
                VLOG(5) << "Ignoring value that contains a new line character.";
                return;
              }
              std::vector<Token> line_tokens = ExtendTokensToEndOfString(
                  prompt_value, TokenizeNameForPrefixSearches(prompt_value));
              naive_bayes::Event event_key(cpp_string.OriginalString());
              std::vector<naive_bayes::FeaturesSet>* features_output = nullptr;
              if (filter_tokens.empty()) {
                VLOG(6) << "Accepting value (empty filters): "
                        << line.ToString();
                features_output = &history_data[event_key];
              } else if (auto match =
                             FindFilterPositions(filter_tokens, line_tokens);
                         match.has_value()) {
                VLOG(5) << "Accepting value, produced a match: "
                        << line.ToString();
                features_output = &history_data[event_key];
                history_prompt_tokens.insert(
                    {event_key, std::move(match.value())});
              } else {
                VLOG(6) << "Ignoring value, no match: " << line.ToString();
                return;
              }
              naive_bayes::FeaturesSet current_features;
              for (auto& [key, value] : *line_keys) {
                if (key != L"prompt") {
                  current_features.insert(naive_bayes::Feature(
                      key + L":" + QuoteString(value)->ToString()));
                }
              }
              features_output->push_back(std::move(current_features));
            }},
        vm::EscapedString::Parse(range.first->second->ToString()));
    return !abort_notification->HasBeenNotified();
  });

  VLOG(4) << "Matches found: " << history_data.read().size();

  // For sorting.
  naive_bayes::FeaturesSet current_features;
  for (const auto& [name, value] : features) {
    current_features.insert(
        naive_bayes::Feature(name + L":" + QuoteString(value)->ToString()));
  }
  for (const auto& [name, value] : GetSyntheticFeatures(features)) {
    current_features.insert(
        naive_bayes::Feature(name + L":" + QuoteString(value)->ToString()));
  }

  for (naive_bayes::Event& key :
       naive_bayes::Sort(history_data, current_features)) {
    std::vector<TokenAndModifiers> tokens;
    for (const Token& token : history_prompt_tokens[key]) {
      VLOG(6) << "Add token BOLD: " << token;
      tokens.push_back({token, LineModifierSet{LineModifier::BOLD}});
    }
    output.lines.push_back(
        ColorizeLine(NewLazyString(key.read()), std::move(tokens)));
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
            BufferContents history_contents;
            FilterSortHistorySyncOutput output =
                FilterSortHistorySync(MakeNonNullShared<Notification>(), L"",
                                      history_contents.copy(), features);
            CHECK(output.lines.empty());
          }},
     {.name = L"NoMatch",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            BufferContents history_contents;
            history_contents.push_back(L"prompt:\"foobar\"");
            history_contents.push_back(L"prompt:\"foo\"");
            FilterSortHistorySyncOutput output = FilterSortHistorySync(
                MakeNonNullShared<Notification>(), L"quux",
                history_contents.copy(), features);
            CHECK(output.lines.empty());
          }},
     {.name = L"MatchAfterEscape",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            BufferContents history_contents;
            history_contents.push_back(L"prompt:\"foo\\\\nbardo\"");
            FilterSortHistorySyncOutput output = FilterSortHistorySync(
                MakeNonNullShared<Notification>(), L"nbar",
                history_contents.copy(), features);
            CHECK_EQ(output.lines.size(), 1ul);
            Line& line = output.lines[0].value();
            CHECK(line.ToString() == L"foo\\nbardo");

            const std::map<ColumnNumber, LineModifierSet> modifiers =
                line.modifiers();
            for (const auto& m : modifiers) {
              LOG(INFO) << "Modifiers: " << m.first << ": " << m.second;
            }

            {
              auto s = modifiers.find(ColumnNumber(4));
              CHECK(s != modifiers.end());
              LOG(INFO) << "Modifiers found: " << s->second;
              CHECK_EQ(s->second, LineModifierSet{LineModifier::BOLD});
            }
            {
              auto s = modifiers.find(ColumnNumber(8));
              CHECK(s != modifiers.end());
              CHECK_EQ(s->second, LineModifierSet{});
            }
          }},
     {.name = L"MatchIncludingEscapeCorrectlyHandled",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            BufferContents history_contents;
            history_contents.push_back(L"prompt:\"foo\\nbar\"");
            FilterSortHistorySyncOutput output = FilterSortHistorySync(
                MakeNonNullShared<Notification>(), L"nbar",
                history_contents.copy(), features);
            CHECK(output.lines.empty());
          }},
     {.name = L"IgnoresInvalidEntries",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            BufferContents history_contents;
            history_contents.push_back(L"prompt:\"foobar \\\"");
            history_contents.push_back(L"prompt:\"foo\"");
            history_contents.push_back(L"prompt:\"foo\n bar\"");
            history_contents.push_back(L"prompt:\"foo \\o bar \\\"");
            FilterSortHistorySyncOutput output =
                FilterSortHistorySync(MakeNonNullShared<Notification>(), L"f",
                                      history_contents.copy(), features);
            CHECK_EQ(output.lines.size(), 1ul);
            CHECK(output.lines[0]->ToString() == L"foo");
          }},
     {.name = L"HistoryWithRawNewLine",
      .callback =
          [] {
            std::unordered_multimap<std::wstring,
                                    NonNull<std::shared_ptr<LazyString>>>
                features;
            BufferContents history_contents;
            history_contents.push_back(L"prompt:\"ls\n\"");
            FilterSortHistorySyncOutput output =
                FilterSortHistorySync(MakeNonNullShared<Notification>(), L"ls",
                                      history_contents.copy(), features);
            CHECK(output.lines.empty());
          }},
     {.name = L"HistoryWithEscapedNewLine", .callback = [] {
        std::unordered_multimap<std::wstring,
                                NonNull<std::shared_ptr<LazyString>>>
            features;
        BufferContents history_contents;
        history_contents.push_back(L"prompt:\"ls\\n\"");
        FilterSortHistorySyncOutput output =
            FilterSortHistorySync(MakeNonNullShared<Notification>(), L"ls",
                                  history_contents.copy(), features);
        CHECK_EQ(output.lines.size(), 0ul);
      }}});

futures::Value<gc::Root<OpenBuffer>> FilterHistory(
    EditorState& editor_state, gc::Root<OpenBuffer> history_buffer,
    NonNull<std::shared_ptr<Notification>> abort_notification,
    std::wstring filter) {
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
                  abort_notification, filter](EmptyValue) {
        NonNull<std::shared_ptr<BufferContents>> history_contents =
            history_buffer.ptr()->contents().copy();
        return editor_state.thread_pool().Run(std::bind_front(
            FilterSortHistorySync, abort_notification, filter, history_contents,
            GetCurrentFeatures(editor_state)));
      })
      .Transform([&editor_state, abort_notification, filter_buffer_root,
                  &filter_buffer](FilterSortHistorySyncOutput output) {
        LOG(INFO) << "Receiving output from history evaluator.";
        if (!output.errors.empty()) {
          editor_state.status().SetExpiringInformationText(
              output.errors.front().read());
        }
        if (!abort_notification->HasBeenNotified()) {
          for (auto& line : output.lines) {
            filter_buffer.AppendRawLine(line);
          }

          if (filter_buffer.lines_size() > LineNumberDelta(1)) {
            VLOG(5) << "Erasing the first (empty) line.";
            CHECK(filter_buffer.LineAt(LineNumber())->empty());
            filter_buffer.EraseLines(LineNumber(), LineNumber().next());
          }
        }
        return filter_buffer_root;
      });
}

gc::Root<OpenBuffer> GetPromptBuffer(const PromptOptions& options,
                                     EditorState& editor_state) {
  BufferName name(L"- prompt");
  if (auto it = editor_state.buffers()->find(name);
      it != editor_state.buffers()->end()) {
    gc::Root<OpenBuffer> buffer_root = it->second;
    OpenBuffer& buffer = buffer_root.ptr().value();
    buffer.ClearContents(BufferContents::CursorsBehavior::kAdjust);
    CHECK_EQ(buffer.EndLine(), LineNumber(0));
    CHECK(buffer.contents().back()->empty());
    buffer.Set(buffer_variables::contents_type, options.prompt_contents_type);
    buffer.Reload();
    return buffer_root;
  }
  gc::Root<OpenBuffer> buffer_root =
      OpenBuffer::New({.editor = editor_state, .name = name});
  OpenBuffer& buffer = buffer_root.ptr().value();
  buffer.Set(buffer_variables::allow_dirty_delete, true);
  buffer.Set(buffer_variables::show_in_buffers_list, false);
  buffer.Set(buffer_variables::delete_into_paste_buffer, false);
  buffer.Set(buffer_variables::save_on_close, false);
  buffer.Set(buffer_variables::persist_state, false);
  buffer.Set(buffer_variables::contents_type, options.prompt_contents_type);
  auto insert_results = editor_state.buffers()->insert_or_assign(
      BufferName(L"- prompt"), buffer_root);
  CHECK(insert_results.second);
  return buffer_root;
}

// Holds the state required to show and update a prompt.
class PromptState {
 public:
  PromptState(PromptOptions options)
      : editor_state_(options.editor_state),
        status_buffer_([&]() -> std::optional<gc::Root<OpenBuffer>> {
          if (options.status == PromptOptions::Status::kEditor)
            return std::nullopt;
          auto active_buffers = editor_state_.active_buffers();
          return active_buffers.size() == 1
                     ? active_buffers[0]
                     : std::optional<gc::Root<OpenBuffer>>();
        }()),
        status_(status_buffer_.has_value() ? status_buffer_->ptr()->status()
                                           : editor_state_.status()),
        original_modifiers_(editor_state_.modifiers()) {
    editor_state_.set_modifiers(Modifiers());
  }

  // The prompt has disappeared.
  bool IsGone() const { return status().GetType() != Status::Type::kPrompt; }

  Status& status() const { return status_; }

  void Reset() {
    status().Reset();
    editor_state_.set_modifiers(original_modifiers_);
  }

 private:
  EditorState& editor_state_;
  // If the status is associated with a buffer, we capture it here; that allows
  // us to ensure that the status won't be deallocated under our feet (when the
  // buffer is ephemeral).
  const std::optional<gc::Root<OpenBuffer>> status_buffer_;
  Status& status_;
  const Modifiers original_modifiers_;
};

// Holds the state for rendering information from asynchronous operations given
// a frozen state of the status.
class PromptRenderState {
 public:
  PromptRenderState(NonNull<std::shared_ptr<PromptState>> prompt_state)
      : prompt_state_(std::move(prompt_state)),
        status_version_(prompt_state_->status()
                            .prompt_extra_information()
                            ->StartNewVersion()) {}

  ~PromptRenderState() {
    auto consumer = prompt_state_->status().prompt_extra_information();
    if (consumer != nullptr) consumer->MarkVersionDone(status_version_);
  }

  // The prompt has disappeared.
  bool IsGone() const { return prompt_state_->IsGone(); }

  template <typename T>
  void SetStatusValue(StatusPromptExtraInformationKey key, T value) {
    CHECK(prompt_state_->status().GetType() == Status::Type::kPrompt);
    prompt_state_->status().prompt_extra_information()->SetValue(
        key, status_version_, value);
  }

  template <typename T>
  void SetStatusValues(T container) {
    if (IsGone()) return;
    for (const auto& [key, value] : container) SetStatusValue(key, value);
  }

 private:
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;

  // The version of the status for which we're collecting information. This is
  // incremented by the PromptState constructor.
  const int status_version_;
};

class HistoryScrollBehavior : public ScrollBehavior {
 public:
  HistoryScrollBehavior(
      futures::ListenableValue<gc::Root<OpenBuffer>> filtered_history,
      NonNull<std::shared_ptr<LazyString>> original_input,
      NonNull<std::shared_ptr<PromptState>> prompt_state)
      : filtered_history_(std::move(filtered_history)),
        original_input_(std::move(original_input)),
        prompt_state_(std::move(prompt_state)),
        previous_context_(prompt_state_->status().context()) {
    CHECK(prompt_state_->status().GetType() == Status::Type::kPrompt ||
          prompt_state_->IsGone());
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
    if (delta == LineNumberDelta(+1) && !filtered_history_.has_value()) {
      ReplaceContents(buffer, MakeNonNullShared<BufferContents>());
      return;
    }
    filtered_history_.AddListener(
        [delta, buffer_root = buffer.NewRoot(), &buffer,
         original_input = original_input_, prompt_state = prompt_state_,
         previous_context =
             previous_context_](gc::Root<OpenBuffer> history_root) {
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
              if (history.current_line() != nullptr) {
                line_to_insert = history.current_line();
              }
            } else if (prompt_state->status().context() != previous_context) {
              prompt_state->status().set_context(previous_context);
              line_to_insert = std::make_shared<Line>(original_input);
            }
          }
          NonNull<std::shared_ptr<BufferContents>> contents_to_insert;
          VisitPointer(
              line_to_insert,
              [&](NonNull<std::shared_ptr<const Line>> line) {
                VLOG(5) << "Inserting line: " << line->ToString();
                contents_to_insert->AppendToLine(LineNumber(), line.value());
              },
              [] {});
          ReplaceContents(buffer, contents_to_insert);
        });
  }

  static void ReplaceContents(
      OpenBuffer& buffer,
      NonNull<std::shared_ptr<BufferContents>> contents_to_insert) {
    buffer.ApplyToCursors(transformation::Delete{
        .modifiers = {.structure = StructureLine(),
                      .paste_buffer_behavior =
                          Modifiers::PasteBufferBehavior::kDoNothing,
                      .boundary_begin = Modifiers::LIMIT_CURRENT,
                      .boundary_end = Modifiers::LIMIT_CURRENT},
        .initiator = transformation::Delete::Initiator::kInternal});

    buffer.ApplyToCursors(transformation::Insert{
        .contents_to_insert = std::move(contents_to_insert)});
  }

  const futures::ListenableValue<gc::Root<OpenBuffer>> filtered_history_;
  const NonNull<std::shared_ptr<LazyString>> original_input_;
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;
  const std::optional<gc::Root<OpenBuffer>> previous_context_;
};

class HistoryScrollBehaviorFactory : public ScrollBehaviorFactory {
 public:
  HistoryScrollBehaviorFactory(
      EditorState& editor_state, std::wstring prompt,
      gc::Root<OpenBuffer> history,
      NonNull<std::shared_ptr<PromptState>> prompt_state,
      gc::Root<OpenBuffer> buffer)
      : editor_state_(editor_state),
        prompt_(std::move(prompt)),
        history_(std::move(history)),
        prompt_state_(std::move(prompt_state)),
        buffer_(std::move(buffer)) {}

  futures::Value<NonNull<std::unique_ptr<ScrollBehavior>>> Build(
      NonNull<std::shared_ptr<Notification>> abort_notification) override {
    CHECK_GT(buffer_.ptr()->lines_size(), LineNumberDelta(0));
    NonNull<std::shared_ptr<LazyString>> input =
        buffer_.ptr()->contents().at(LineNumber(0))->contents();
    return futures::Past(MakeNonNullUnique<HistoryScrollBehavior>(
        futures::ListenableValue(
            FilterHistory(editor_state_, history_, abort_notification,
                          input->ToString())
                .Transform([input](gc::Root<OpenBuffer> history_filtered) {
                  history_filtered.ptr()->set_current_position_line(
                      LineNumber(0) +
                      history_filtered.ptr()->contents().size());
                  return history_filtered;
                })),
        input, prompt_state_));
  }

 private:
  EditorState& editor_state_;
  const std::wstring prompt_;
  const gc::Root<OpenBuffer> history_;
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;
  const gc::Root<OpenBuffer> buffer_;
};

class LinePromptCommand : public Command {
 public:
  LinePromptCommand(EditorState& editor_state, std::wstring description,
                    std::function<PromptOptions()> options_supplier)
      : editor_state_(editor_state),
        description_(std::move(description)),
        options_supplier_(std::move(options_supplier)) {}

  std::wstring Description() const override { return description_; }
  std::wstring Category() const override { return L"Prompt"; }

  void ProcessInput(wint_t) override {
    auto buffer = editor_state_.current_buffer();
    if (!buffer.has_value()) return;
    auto options = options_supplier_();
    if (editor_state_.structure() == StructureLine()) {
      editor_state_.ResetStructure();
      VisitPointer(
          buffer->ptr()->current_line(),
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

 private:
  EditorState& editor_state_;
  const std::wstring description_;
  const std::function<PromptOptions()> options_supplier_;
};

// status_buffer is the buffer with the contents of the prompt. tokens_future is
// received as a future so that we can detect if the prompt input changes
// between the time when `ColorizePrompt` is executed and the time when the
// tokens become available.
void ColorizePrompt(OpenBuffer& status_buffer,
                    NonNull<std::shared_ptr<PromptState>> prompt_state,
                    NonNull<std::shared_ptr<Notification>> abort_notification,
                    const NonNull<std::shared_ptr<const Line>>& original_line,
                    ColorizePromptOptions options) {
  CHECK_EQ(status_buffer.lines_size(), LineNumberDelta(1));
  if (prompt_state->IsGone()) {
    LOG(INFO) << "Status is no longer a prompt, aborting colorize prompt.";
    return;
  }

  if (prompt_state->status().prompt_buffer().has_value() &&
      &prompt_state->status().prompt_buffer()->ptr().value() !=
          &status_buffer) {
    LOG(INFO) << "Prompt buffer has changed, aborting colorize prompt.";
    return;
  }
  if (abort_notification->HasBeenNotified()) {
    LOG(INFO) << "Abort notification notified, aborting colorize prompt.";
    return;
  }

  CHECK_EQ(status_buffer.lines_size(), LineNumberDelta(1));
  auto line = status_buffer.LineAt(LineNumber(0));
  if (original_line->ToString() != line->ToString()) {
    LOG(INFO) << "Line has changed, ignoring prompt colorize update.";
    return;
  }

  status_buffer.AppendRawLine(
      ColorizeLine(line->contents(), std::move(options.tokens)));
  status_buffer.EraseLines(LineNumber(0), LineNumber(1));
  if (options.context.has_value()) {
    prompt_state->status().set_context(options.context.value());
  }
}

template <typename T0, typename T1>
futures::Value<std::tuple<T0, T1>> JoinValues(futures::Value<T0> f0,
                                              futures::Value<T1> f1) {
  auto shared_f1 = std::make_shared<futures::Value<T1>>(std::move(f1));
  return f0.Transform([shared_f1 = std::move(shared_f1)](T0 t0) mutable {
    return shared_f1->Transform([t0 = std::move(t0)](T1 t1) mutable {
      return std::tuple{std::move(t0), std::move(t1)};
    });
  });
}
}  // namespace

HistoryFile HistoryFileFiles() { return HistoryFile(L"files"); }
HistoryFile HistoryFileCommands() { return HistoryFile(L"commands"); }

// input must not be escaped.
void AddLineToHistory(EditorState& editor, const HistoryFile& history_file,
                      NonNull<std::shared_ptr<LazyString>> input) {
  if (input->size().IsZero()) return;
  GetHistoryBuffer(editor, history_file)
      .Transform([history_line = BuildHistoryLine(editor, input)](
                     gc::Root<OpenBuffer> history) {
        history.ptr()->AppendLine(history_line);
        return Success();
      });
}

void Prompt(PromptOptions options) {
  CHECK(options.handler != nullptr);
  EditorState& editor_state = options.editor_state;
  HistoryFile history_file = options.history_file;
  GetHistoryBuffer(editor_state, history_file)
      .SetConsumer([options = std::move(options),
                    &editor_state](gc::Root<OpenBuffer> history) {
        history.ptr()->set_current_position_line(
            LineNumber(0) + history.ptr()->contents().size());

        gc::Root<OpenBuffer> prompt_buffer =
            GetPromptBuffer(options, editor_state);

        auto prompt_state = MakeNonNullShared<PromptState>(options);

        prompt_buffer.ptr()->ApplyToCursors(transformation::Insert(
            {.contents_to_insert = MakeNonNullUnique<BufferContents>(
                 MakeNonNullShared<Line>(options.initial_value))}));

        // Notification that can be used to abort an ongoing execution of
        // `colorize_options_provider`. Every time we call
        // `colorize_options_provider` from modify_handler, we notify the
        // previous notification and set this to a new notification that will be
        // given to the `colorize_options_provider`.
        auto abort_notification_ptr =
            std::make_shared<NonNull<std::shared_ptr<Notification>>>();
        InsertModeOptions insert_mode_options{
            .editor_state = editor_state,
            .buffers = {{prompt_buffer}},
            .modify_handler =
                [&editor_state, history, prompt_state, options,
                 abort_notification_ptr](OpenBuffer& buffer) {
                  NonNull<std::shared_ptr<LazyString>> line =
                      buffer.contents().at(LineNumber())->contents();
                  if (options.colorize_options_provider == nullptr ||
                      prompt_state->status().GetType() !=
                          Status::Type::kPrompt) {
                    return futures::Past(EmptyValue());
                  }
                  auto prompt_render_state =
                      std::make_shared<PromptRenderState>(prompt_state);
                  NonNull<std::unique_ptr<ProgressChannel>> progress_channel(
                      buffer.work_queue(),
                      [prompt_render_state](
                          ProgressInformation extra_information) {
                        prompt_render_state->SetStatusValues(
                            extra_information.values);
                        prompt_render_state->SetStatusValues(
                            extra_information.counters);
                      },
                      WorkQueueChannelConsumeMode::kAll);
                  (*abort_notification_ptr)->Notify();
                  *abort_notification_ptr =
                      NonNull<std::shared_ptr<Notification>>();
                  return JoinValues(
                             FilterHistory(editor_state, history,
                                           *abort_notification_ptr,
                                           line->ToString())
                                 .Transform([prompt_render_state](
                                                gc::Root<OpenBuffer>
                                                    filtered_history) {
                                   LOG(INFO)
                                       << "Propagating history information "
                                          "to status.";
                                   if (!prompt_render_state->IsGone()) {
                                     bool last_line_empty =
                                         filtered_history.ptr()
                                             ->LineAt(filtered_history.ptr()
                                                          ->EndLine())
                                             ->empty();
                                     prompt_render_state->SetStatusValue(
                                         StatusPromptExtraInformationKey(
                                             L"history"),
                                         filtered_history.ptr()
                                                 ->lines_size()
                                                 .read() -
                                             (last_line_empty ? 1 : 0));
                                   }
                                   return EmptyValue();
                                 }),
                             options
                                 .colorize_options_provider(
                                     line, std::move(progress_channel),
                                     *abort_notification_ptr)
                                 .Transform(
                                     [buffer = buffer.NewRoot(), prompt_state,
                                      abort_notification_ptr,
                                      original_line =
                                          buffer.contents().at(LineNumber(0))](
                                         ColorizePromptOptions
                                             colorize_prompt_options) {
                                       LOG(INFO) << "Calling ColorizePrompt "
                                                    "with results.";
                                       ColorizePrompt(buffer.ptr().value(),
                                                      prompt_state,
                                                      *abort_notification_ptr,
                                                      original_line,
                                                      colorize_prompt_options);
                                       return EmptyValue();
                                     }))
                      .Transform([](auto) { return EmptyValue(); });
                },
            .scroll_behavior = MakeNonNullShared<HistoryScrollBehaviorFactory>(
                editor_state, options.prompt, history, prompt_state,
                prompt_buffer),
            .escape_handler =
                [&editor_state, options, prompt_state]() {
                  LOG(INFO) << "Running escape_handler from Prompt.";
                  prompt_state->Reset();

                  if (options.cancel_handler) {
                    VLOG(5) << "Running cancel handler.";
                    options.cancel_handler();
                  } else {
                    VLOG(5) << "Running handler on empty input.";
                    options.handler(EmptyString());
                  }
                  editor_state.set_keyboard_redirect(nullptr);
                },
            .new_line_handler =
                [&editor_state, options, prompt_state](OpenBuffer& buffer) {
                  NonNull<std::shared_ptr<LazyString>> input =
                      buffer.current_line()->contents();
                  AddLineToHistory(editor_state, options.history_file, input);
                  auto ensure_survival_of_current_closure =
                      editor_state.set_keyboard_redirect(nullptr);
                  prompt_state->Reset();
                  return options.handler(input);
                },
            .start_completion =
                [&editor_state, options, prompt_state](OpenBuffer& buffer) {
                  auto input = buffer.current_line()->contents()->ToString();
                  LOG(INFO) << "Triggering predictions from: " << input;
                  CHECK(prompt_state->status().prompt_extra_information() !=
                        nullptr);
                  gc::Root<OpenBuffer> buffer_root = buffer.NewRoot();
                  Predict({.editor_state = editor_state,
                           .predictor = options.predictor,
                           .input_buffer = buffer_root,
                           .input_selection_structure = StructureLine(),
                           .source_buffers = options.source_buffers})
                      .SetConsumer([&editor_state, options, buffer_root,
                                    prompt_state, input](
                                       std::optional<PredictResults> results) {
                        if (!results.has_value()) return;
                        if (results.value().common_prefix.has_value() &&
                            !results.value().common_prefix.value().empty() &&
                            input != results.value().common_prefix.value()) {
                          LOG(INFO) << "Prediction advanced from " << input
                                    << " to " << results.value();

                          buffer_root.ptr()->ApplyToCursors(
                              transformation::Delete{
                                  .modifiers =
                                      {.structure = StructureLine(),
                                       .paste_buffer_behavior = Modifiers::
                                           PasteBufferBehavior::kDoNothing,
                                       .boundary_begin =
                                           Modifiers::LIMIT_CURRENT,
                                       .boundary_end =
                                           Modifiers::LIMIT_CURRENT},
                                  .initiator = transformation::Delete::
                                      Initiator::kInternal});

                          NonNull<std::shared_ptr<LazyString>> line =
                              NewLazyString(
                                  results.value().common_prefix.value());

                          buffer_root.ptr()->ApplyToCursors(
                              transformation::Insert(
                                  {.contents_to_insert =
                                       MakeNonNullUnique<BufferContents>(
                                           MakeNonNullShared<Line>(line))}));
                          if (options.colorize_options_provider != nullptr) {
                            CHECK(prompt_state->status().GetType() ==
                                  Status::Type::kPrompt);
                            auto prompt_render_state =
                                std::make_shared<PromptRenderState>(
                                    prompt_state);
                            options
                                .colorize_options_provider(
                                    line,
                                    MakeNonNullUnique<ProgressChannel>(
                                        buffer_root.ptr()->work_queue(),
                                        [](ProgressInformation) {
                                          /* Nothing for now. */
                                        },
                                        WorkQueueChannelConsumeMode::kAll),
                                    NonNull<std::shared_ptr<Notification>>())
                                // Can't use std::bind_front: need to return
                                // success.
                                .Transform([buffer_root, prompt_state,
                                            prompt_render_state,
                                            original_line =
                                                buffer_root.ptr()
                                                    ->contents()
                                                    .at(LineNumber(0))](
                                               ColorizePromptOptions
                                                   colorize_prompt_options) {
                                  ColorizePrompt(
                                      buffer_root.ptr().value(), prompt_state,
                                      NonNull<std::shared_ptr<Notification>>(),
                                      original_line, colorize_prompt_options);
                                  return Success();
                                });
                          }
                          return;
                        }
                        LOG(INFO) << "Prediction didn't advance.";
                        auto buffers = editor_state.buffers();
                        auto name = PredictionsBufferName();
                        if (auto it = buffers->find(name);
                            it != buffers->end()) {
                          it->second.ptr()->set_current_position_line(
                              LineNumber(0));
                          editor_state.set_current_buffer(
                              it->second, CommandArgumentModeApplyMode::kFinal);
                          if (!editor_state.status()
                                   .prompt_buffer()
                                   .has_value()) {
                            it->second.ptr()->status().CopyFrom(
                                prompt_state->status());
                          }
                        } else {
                          editor_state.status().SetWarningText(
                              L"Error: Predict: predictions buffer not "
                              L"found: " +
                              name.read());
                        }
                      });
                  return true;
                }};
        EnterInsertMode(insert_mode_options);

        // We do this after `EnterInsertMode` because `EnterInsertMode` resets
        // the status.
        prompt_state->status().set_prompt(options.prompt, prompt_buffer);
        insert_mode_options.modify_handler(prompt_buffer.ptr().value());
      });
}

NonNull<std::unique_ptr<Command>> NewLinePromptCommand(
    EditorState& editor_state, std::wstring description,
    std::function<PromptOptions()> options_supplier) {
  return MakeNonNullUnique<LinePromptCommand>(
      editor_state, std::move(description), std::move(options_supplier));
}

}  // namespace afc::editor
