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
#include "src/language/wstring.h"
#include "src/lazy_string_append.h"
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
#include "src/vm/public/value.h"

namespace afc::editor {
using concurrent::Notification;
using concurrent::WorkQueueChannelConsumeMode;
using infrastructure::Path;
using infrastructure::PathComponent;
using language::EmptyValue;
using language::Error;
using language::MakeNonNullShared;
using language::NonNull;
using language::Success;
using language::ValueOrError;
namespace {

std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
GetCurrentFeatures(EditorState& editor) {
  std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
      output;
  for (auto& [_, buffer] : *editor.buffers()) {
    // We have to deal with nullptr buffers here because this gets called after
    // the entry for the new buffer has been inserted to the editor, but before
    // the buffer has actually been created.
    if (buffer != nullptr &&
        buffer->Read(buffer_variables::show_in_buffers_list) &&
        editor.buffer_tree().GetBufferIndex(buffer.get()).has_value()) {
      output.insert(
          {L"name", NewLazyString(buffer->Read(buffer_variables::name))});
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
  for (const auto& [name, value] : input) {
    if (name == L"name") {
      auto value_str = value->ToString();
      auto value_path = Path::FromString(value_str);
      if (value_path.IsError()) continue;
      if (auto directory = value_path.value().Dirname();
          !directory.IsError() && directory.value() != Path::LocalDirectory()) {
        directories.insert(directory.value());
      }
      if (std::optional<std::wstring> extension =
              value_path.value().extension();
          extension.has_value()) {
        extensions.insert(extension.value());
      }
    }
  }
  for (auto& dir : directories) {
    output.insert({L"directory", NewLazyString(dir.read())});
  }
  for (auto& extension : extensions) {
    output.insert({L"extension", NewLazyString(std::move(extension))});
  }
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

futures::Value<std::shared_ptr<OpenBuffer>> GetHistoryBuffer(
    EditorState& editor_state, const HistoryFile& name) {
  BufferName buffer_name(L"- history: " + name.read());
  auto it = editor_state.buffers()->find(buffer_name);
  if (it != editor_state.buffers()->end()) {
    return futures::Past(it->second);
  }
  return OpenFile({.editor_state = editor_state,
                   .name = buffer_name,
                   .path = editor_state.edge_path().empty()
                               ? std::nullopt
                               : std::make_optional(Path::Join(
                                     editor_state.edge_path().front(),
                                     PathComponent::FromString(name.read() +
                                                               L"_history")
                                         .value())),
                   .insertion_type = BuffersList::AddBufferType::kIgnore})
      .Transform(
          [&editor_state](
              std::map<BufferName, std::shared_ptr<OpenBuffer>>::iterator it) {
            CHECK(it != editor_state.buffers()->end());
            CHECK(it->second != nullptr);
            it->second->Set(buffer_variables::save_on_close, true);
            it->second->Set(buffer_variables::trigger_reload_on_buffer_write,
                            false);
            it->second->Set(buffer_variables::show_in_buffers_list, false);
            it->second->Set(buffer_variables::atomic_lines, true);
            it->second->Set(buffer_variables::close_after_idle_seconds, 20.0);
            if (!editor_state.has_current_buffer()) {
              // Seems lame, but what can we do?
              editor_state.set_current_buffer(
                  it->second, CommandArgumentModeApplyMode::kFinal);
            }
            return it->second;
          });
}

ValueOrError<
    std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>>
ParseHistoryLine(const NonNull<std::shared_ptr<LazyString>>& line) {
  std::unordered_multimap<std::wstring, NonNull<std::shared_ptr<LazyString>>>
      output;
  for (const auto& token : TokenizeBySpaces(*line)) {
    auto colon = token.value.find(':');
    if (colon == string::npos) {
      return Error(L"Unable to parse prompt line (no colon found in token): " +
                   line->ToString());
    }
    ColumnNumber value_start = token.begin + ColumnNumberDelta(colon);
    ++value_start;  // Skip the colon.
    ColumnNumber value_end = token.end;
    if (value_end == value_start || line->get(value_start) != '\"' ||
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
  for (auto& additional_features : GetSyntheticFeatures(output)) {
    output.insert(additional_features);
  }
  return Success(std::move(output));
}

NonNull<std::shared_ptr<LazyString>> QuoteString(
    NonNull<std::shared_ptr<LazyString>> src) {
  return StringAppend(NewLazyString(L"\""),
                      NewLazyString(CppEscapeString(src->ToString())),
                      NewLazyString(L"\""));
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

// TODO(easy, 2022-04-22): Use NonNull for history_buffer.
futures::Value<std::shared_ptr<OpenBuffer>> FilterHistory(
    EditorState& editor_state, std::shared_ptr<OpenBuffer> history_buffer,
    NonNull<std::shared_ptr<Notification>> abort_notification,
    std::wstring filter) {
  BufferName name(L"- history filter: " + history_buffer->name().read() +
                  L": " + filter);
  auto filter_buffer = OpenBuffer::New({.editor = editor_state, .name = name});
  filter_buffer->Set(buffer_variables::allow_dirty_delete, true);
  filter_buffer->Set(buffer_variables::show_in_buffers_list, false);
  filter_buffer->Set(buffer_variables::delete_into_paste_buffer, false);
  filter_buffer->Set(buffer_variables::atomic_lines, true);
  filter_buffer->Set(buffer_variables::line_width, 1);

  struct Output {
    std::deque<std::wstring> errors;
    std::deque<NonNull<std::shared_ptr<Line>>> lines;
  };

  return history_buffer->WaitForEndOfFile()
      .Transform([&editor_state, filter_buffer, history_buffer,
                  abort_notification, filter](EmptyValue) {
        return editor_state.thread_pool().Run([abort_notification, filter,
                                               history_contents =
                                                   std::shared_ptr<
                                                       BufferContents>(
                                                       history_buffer
                                                           ->contents()
                                                           .copy()),
                                               features = GetCurrentFeatures(
                                                   editor_state)]() -> Output {
          if (abort_notification->HasBeenNotified()) return Output{};
          Output output;
          // Sets of features for each unique `prompt` value in the history.
          naive_bayes::History history_data({});
          // Tokens by parsing the `prompt` value in the history.
          std::unordered_map<naive_bayes::Event, std::vector<Token>>
              history_prompt_tokens;
          std::vector<Token> filter_tokens =
              TokenizeBySpaces(*NewLazyString(filter));
          history_contents->EveryLine([&](LineNumber, const Line& line) {
            auto warn_if = [&](bool condition, wstring description) {
              if (condition) {
                output.errors.push_back(description + L": " +
                                        line.contents()->ToString());
              }
              return condition;
            };
            if (line.empty()) return true;
            auto line_keys = ParseHistoryLine(line.contents());
            if (line_keys.IsError()) {
              output.errors.push_back(line_keys.error().description);
              return !abort_notification->HasBeenNotified();
            }
            auto range = line_keys.value().equal_range(L"prompt");
            int prompt_count = std::distance(range.first, range.second);
            if (warn_if(prompt_count == 0,
                        L"Line is missing `prompt` section") ||
                warn_if(prompt_count != 1,
                        L"Line has multiple `prompt` sections")) {
              return !abort_notification->HasBeenNotified();
            }

            auto prompt_value_optional =
                CppUnescapeString(range.first->second->ToString());
            if (!prompt_value_optional.has_value()) {
              LOG(INFO) << "Unable to unescape string: "
                        << range.first->second->ToString();
              return !abort_notification->HasBeenNotified();
            }
            NonNull<std::shared_ptr<LazyString>> prompt_value =
                NewLazyString(*prompt_value_optional);
            VLOG(8) << "Considering history value: "
                    << prompt_value->ToString();
            std::vector<Token> line_tokens = ExtendTokensToEndOfString(
                prompt_value, TokenizeNameForPrefixSearches(prompt_value));
            naive_bayes::Event event_key(prompt_value->ToString());
            std::vector<naive_bayes::FeaturesSet>* features_output = nullptr;
            if (filter_tokens.empty()) {
              VLOG(6) << "Accepting value (empty filters): " << line.ToString();
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
              return !abort_notification->HasBeenNotified();
            }
            std::unordered_set<naive_bayes::Feature> features;
            for (auto& [key, value] : line_keys.value()) {
              if (key != L"prompt") {
                features.insert(naive_bayes::Feature(
                    key + L":" + QuoteString(value)->ToString()));
              }
            }
            features_output->push_back(
                naive_bayes::FeaturesSet(std::move(features)));
            return !abort_notification->HasBeenNotified();
          });

          VLOG(4) << "Matches found: " << history_data.read().size();

          // For sorting.
          std::unordered_set<naive_bayes::Feature> current_features;
          for (const auto& [name, value] : features) {
            current_features.insert(naive_bayes::Feature(
                name + L":" + QuoteString(value)->ToString()));
          }
          for (const auto& [name, value] : GetSyntheticFeatures(features)) {
            current_features.insert(naive_bayes::Feature(
                name + L":" + QuoteString(value)->ToString()));
          }

          for (naive_bayes::Event& key : naive_bayes::Sort(
                   history_data,
                   afc::naive_bayes::FeaturesSet(current_features))) {
            std::vector<TokenAndModifiers> tokens;
            for (auto token : history_prompt_tokens[key]) {
              tokens.push_back({token, LineModifierSet{LineModifier::BOLD}});
            }
            output.lines.push_back(
                ColorizeLine(NewLazyString(key.read()), std::move(tokens)));
          }
          return output;
        });
      })
      .Transform(
          [&editor_state, abort_notification, filter_buffer](Output output) {
            LOG(INFO) << "Receiving output from history evaluator.";
            if (!output.errors.empty()) {
              editor_state.status().SetExpiringInformationText(
                  output.errors.front());
            }
            if (!abort_notification->HasBeenNotified()) {
              for (auto& line : output.lines) {
                // TODO(easy, 2022-04-22): Get rid of get_shared.
                filter_buffer->AppendRawLine(line);
              }

              if (filter_buffer->lines_size() > LineNumberDelta(1)) {
                VLOG(5) << "Erasing the first (empty) line.";
                CHECK(filter_buffer->LineAt(LineNumber())->empty());
                filter_buffer->EraseLines(LineNumber(), LineNumber().next());
              }
            }
            return filter_buffer;
          });
}

shared_ptr<OpenBuffer> GetPromptBuffer(const PromptOptions& options,
                                       EditorState& editor_state) {
  auto& element =
      *editor_state.buffers()->insert({BufferName(L"- prompt"), nullptr}).first;
  if (element.second == nullptr) {
    element.second =
        OpenBuffer::New({.editor = editor_state, .name = element.first});
    element.second->Set(buffer_variables::allow_dirty_delete, true);
    element.second->Set(buffer_variables::show_in_buffers_list, false);
    element.second->Set(buffer_variables::delete_into_paste_buffer, false);
    element.second->Set(buffer_variables::save_on_close, false);
    element.second->Set(buffer_variables::persist_state, false);
  } else {
    element.second->ClearContents(BufferContents::CursorsBehavior::kAdjust);
    CHECK_EQ(element.second->EndLine(), LineNumber(0));
    CHECK(element.second->contents().back()->empty());
  }
  element.second->Set(buffer_variables::contents_type,
                      options.prompt_contents_type);
  element.second->Reload();
  return element.second;
}

// Holds the state required to show and update a prompt.
class PromptState {
 public:
  PromptState(PromptOptions options)
      : editor_state_(options.editor_state),
        status_buffer_([&]() -> std::shared_ptr<OpenBuffer> {
          if (options.status == PromptOptions::Status::kEditor) return nullptr;
          auto active_buffers = editor_state_.active_buffers();
          return active_buffers.size() == 1 ? active_buffers[0] : nullptr;
        }()),
        status_(status_buffer_ == nullptr ? editor_state_.status()
                                          : status_buffer_->status()),
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
  const std::shared_ptr<OpenBuffer> status_buffer_;
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
  HistoryScrollBehavior(std::shared_ptr<OpenBuffer> history,
                        NonNull<std::shared_ptr<LazyString>> original_input,
                        NonNull<std::shared_ptr<PromptState>> prompt_state)
      : history_(std::move(history)),
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
    auto contents_to_insert = std::make_unique<BufferContents>();

    if (history_ != nullptr &&
        (history_->contents().size() > LineNumberDelta(1) ||
         !history_->LineAt(LineNumber())->empty())) {
      LineColumn position = history_->position();
      position.line = min(position.line.PlusHandlingOverflow(delta),
                          LineNumber() + history_->contents().size());
      history_->set_position(position);
      if (position.line < LineNumber(0) + history_->contents().size()) {
        prompt_state_->status().set_context(history_);
        if (history_->current_line() != nullptr) {
          contents_to_insert->AppendToLine(LineNumber(),
                                           *history_->current_line());
        }
      } else {
        prompt_state_->status().set_context(previous_context_);
        contents_to_insert->AppendToLine(LineNumber(), Line(original_input_));
      }
    }

    buffer.ApplyToCursors(transformation::Delete{
        .modifiers = {
            .structure = StructureLine(),
            .paste_buffer_behavior = Modifiers::PasteBufferBehavior::kDoNothing,
            .boundary_begin = Modifiers::LIMIT_CURRENT,
            .boundary_end = Modifiers::LIMIT_CURRENT}});

    buffer.ApplyToCursors(transformation::Insert{
        .contents_to_insert = std::move(contents_to_insert)});
  }

  const std::shared_ptr<OpenBuffer> history_;
  const NonNull<std::shared_ptr<LazyString>> original_input_;
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;
  const std::shared_ptr<OpenBuffer> previous_context_;
};

class HistoryScrollBehaviorFactory : public ScrollBehaviorFactory {
 public:
  HistoryScrollBehaviorFactory(
      EditorState& editor_state, wstring prompt,
      std::shared_ptr<OpenBuffer> history,
      NonNull<std::shared_ptr<PromptState>> prompt_state,
      std::shared_ptr<OpenBuffer> buffer)
      : editor_state_(editor_state),
        prompt_(std::move(prompt)),
        history_(std::move(history)),
        prompt_state_(std::move(prompt_state)),
        buffer_(std::move(buffer)) {}

  futures::Value<NonNull<std::unique_ptr<ScrollBehavior>>> Build(
      NonNull<std::shared_ptr<Notification>> abort_notification) override {
    CHECK_GT(buffer_->lines_size(), LineNumberDelta(0));
    NonNull<std::shared_ptr<LazyString>> input =
        buffer_->contents().at(LineNumber(0))->contents();
    return FilterHistory(editor_state_, history_, abort_notification,
                         input->ToString())
        .Transform([input, prompt_state = prompt_state_](
                       std::shared_ptr<OpenBuffer> history)
                       -> NonNull<std::unique_ptr<ScrollBehavior>> {
          history->set_current_position_line(LineNumber(0) +
                                             history->contents().size());
          return MakeNonNullUnique<HistoryScrollBehavior>(std::move(history),
                                                          input, prompt_state);
        });
  }

 private:
  EditorState& editor_state_;
  const wstring prompt_;
  const std::shared_ptr<OpenBuffer> history_;
  const NonNull<std::shared_ptr<PromptState>> prompt_state_;
  const std::shared_ptr<OpenBuffer> buffer_;
};

class LinePromptCommand : public Command {
 public:
  LinePromptCommand(EditorState& editor_state, wstring description,
                    std::function<PromptOptions()> options_supplier)
      : editor_state_(editor_state),
        description_(std::move(description)),
        options_supplier_(std::move(options_supplier)) {}

  wstring Description() const override { return description_; }
  wstring Category() const override { return L"Prompt"; }

  void ProcessInput(wint_t) override {
    auto buffer = editor_state_.current_buffer();
    if (buffer == nullptr) return;
    auto options = options_supplier_();
    if (editor_state_.structure() == StructureLine()) {
      editor_state_.ResetStructure();
      auto input = buffer->current_line();
      AddLineToHistory(editor_state_, options.history_file, input->contents());
      options.handler(input->ToString());
    } else {
      Prompt(std::move(options));
    }
  }

 private:
  EditorState& editor_state_;
  const wstring description_;
  const std::function<PromptOptions()> options_supplier_;
};

// status_buffer is the buffer with the contents of the prompt. tokens_future is
// received as a future so that we can detect if the prompt input changes
// between the time when `ColorizePrompt` is executed and the time when the
// tokens become available.
void ColorizePrompt(std::shared_ptr<OpenBuffer> status_buffer,
                    NonNull<std::shared_ptr<PromptState>> prompt_state,
                    NonNull<std::shared_ptr<Notification>> abort_notification,
                    const NonNull<std::shared_ptr<const Line>>& original_line,
                    ColorizePromptOptions options) {
  CHECK(status_buffer != nullptr);
  CHECK_EQ(status_buffer->lines_size(), LineNumberDelta(1));
  if (prompt_state->IsGone()) {
    LOG(INFO) << "Status is no longer a prompt, aborting colorize prompt.";
    return;
  }

  if (prompt_state->status().prompt_buffer() != status_buffer) {
    LOG(INFO) << "Prompt buffer has changed, aborting colorize prompt.";
    return;
  }
  if (abort_notification->HasBeenNotified()) {
    LOG(INFO) << "Abort notification notified, aborting colorize prompt.";
    return;
  }

  CHECK_EQ(status_buffer->lines_size(), LineNumberDelta(1));
  auto line = status_buffer->LineAt(LineNumber(0));
  if (original_line->ToString() != line->ToString()) {
    LOG(INFO) << "Line has changed, ignoring prompt colorize update.";
    return;
  }

  status_buffer->AppendRawLine(
      ColorizeLine(line->contents(), std::move(options.tokens)));
  status_buffer->EraseLines(LineNumber(0), LineNumber(1));
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

void AddLineToHistory(EditorState& editor, const HistoryFile& history_file,
                      NonNull<std::shared_ptr<LazyString>> input) {
  if (input->size().IsZero()) return;
  GetHistoryBuffer(editor, history_file)
      .Transform([history_line = BuildHistoryLine(editor, input)](
                     std::shared_ptr<OpenBuffer> history) {
        CHECK(history != nullptr);
        history->AppendLine(history_line);
        return Success();
      });
}

using std::shared_ptr;
using std::unique_ptr;

void Prompt(PromptOptions options) {
  CHECK(options.handler != nullptr);
  EditorState& editor_state = options.editor_state;
  HistoryFile history_file = options.history_file;
  GetHistoryBuffer(editor_state, history_file)
      .SetConsumer([options = std::move(options),
                    &editor_state](std::shared_ptr<OpenBuffer> history) {
        history->set_current_position_line(LineNumber(0) +
                                           history->contents().size());

        auto buffer = GetPromptBuffer(options, editor_state);
        CHECK(buffer != nullptr);

        auto prompt_state = MakeNonNullShared<PromptState>(options);

        buffer->ApplyToCursors(transformation::Insert(
            {.contents_to_insert = std::make_unique<BufferContents>(
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
            .buffers = {{buffer}},
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
                  auto progress_channel = std::make_unique<ProgressChannel>(
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
                                                std::shared_ptr<OpenBuffer>
                                                    filtered_history) {
                                   LOG(INFO)
                                       << "Propagating history information "
                                          "to status.";
                                   if (!prompt_render_state->IsGone()) {
                                     bool last_line_empty =
                                         filtered_history
                                             ->LineAt(
                                                 filtered_history->EndLine())
                                             ->empty();
                                     prompt_render_state->SetStatusValue(
                                         StatusPromptExtraInformationKey(
                                             L"history"),
                                         filtered_history->lines_size()
                                                 .line_delta -
                                             (last_line_empty ? 1 : 0));
                                   }
                                   return EmptyValue();
                                 }),
                             options
                                 .colorize_options_provider(
                                     line, std::move(progress_channel),
                                     *abort_notification_ptr)
                                 .Transform(
                                     [buffer = buffer.shared_from_this(),
                                      prompt_state, abort_notification_ptr,
                                      original_line =
                                          buffer.contents().at(LineNumber(0))](
                                         ColorizePromptOptions options) {
                                       LOG(INFO) << "Calling ColorizePrompt "
                                                    "with results.";
                                       ColorizePrompt(buffer, prompt_state,
                                                      *abort_notification_ptr,
                                                      original_line, options);
                                       return EmptyValue();
                                     }))
                      .Transform([](auto) { return EmptyValue(); });
                },
            .scroll_behavior = MakeNonNullShared<HistoryScrollBehaviorFactory>(
                editor_state, options.prompt, history, prompt_state, buffer),
            .escape_handler =
                [&editor_state, options, prompt_state]() {
                  LOG(INFO) << "Running escape_handler from Prompt.";
                  prompt_state->Reset();

                  if (options.cancel_handler) {
                    VLOG(5) << "Running cancel handler.";
                    options.cancel_handler();
                  } else {
                    VLOG(5) << "Running handler on empty input.";
                    options.handler(L"");
                  }
                  editor_state.set_keyboard_redirect(nullptr);
                },
            .new_line_handler =
                [&editor_state, options,
                 prompt_state](const std::shared_ptr<OpenBuffer>& buffer) {
                  auto input = buffer->current_line()->contents();
                  AddLineToHistory(editor_state, options.history_file, input);
                  auto ensure_survival_of_current_closure =
                      editor_state.set_keyboard_redirect(nullptr);
                  prompt_state->Reset();
                  return options.handler(input->ToString());
                },
            .start_completion =
                [&editor_state, options,
                 prompt_state](const std::shared_ptr<OpenBuffer>& buffer) {
                  auto input = buffer->current_line()->contents()->ToString();
                  LOG(INFO) << "Triggering predictions from: " << input;
                  CHECK(prompt_state->status().prompt_extra_information() !=
                        nullptr);
                  Predict({.editor_state = editor_state,
                           .predictor = options.predictor,
                           .input_buffer = buffer,
                           .input_selection_structure = StructureLine(),
                           .source_buffers = options.source_buffers})
                      .SetConsumer([&editor_state, options, buffer,
                                    prompt_state, input](
                                       std::optional<PredictResults> results) {
                        if (!results.has_value()) return;
                        if (results.value().common_prefix.has_value() &&
                            !results.value().common_prefix.value().empty() &&
                            input != results.value().common_prefix.value()) {
                          LOG(INFO) << "Prediction advanced from " << input
                                    << " to " << results.value();

                          buffer->ApplyToCursors(transformation::Delete{
                              .modifiers = {
                                  .structure = StructureLine(),
                                  .paste_buffer_behavior = Modifiers::
                                      PasteBufferBehavior::kDoNothing,
                                  .boundary_begin = Modifiers::LIMIT_CURRENT,
                                  .boundary_end = Modifiers::LIMIT_CURRENT}});

                          NonNull<std::shared_ptr<LazyString>> line =
                              NewLazyString(
                                  results.value().common_prefix.value());

                          buffer->ApplyToCursors(transformation::Insert(
                              {.contents_to_insert =
                                   std::make_unique<BufferContents>(
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
                                    std::make_unique<ProgressChannel>(
                                        buffer->work_queue(),
                                        [](ProgressInformation) {
                                          /* Nothing for now. */
                                        },
                                        WorkQueueChannelConsumeMode::kAll),
                                    NonNull<std::shared_ptr<Notification>>())
                                // Can't use std::bind_front: need to return
                                // success.
                                .Transform([buffer, prompt_state,
                                            prompt_render_state,
                                            original_line =
                                                buffer->contents().at(
                                                    LineNumber(0))](
                                               ColorizePromptOptions options) {
                                  ColorizePrompt(
                                      buffer, prompt_state,
                                      NonNull<std::shared_ptr<Notification>>(),
                                      original_line, options);
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
                          it->second->set_current_position_line(LineNumber(0));
                          editor_state.set_current_buffer(
                              it->second, CommandArgumentModeApplyMode::kFinal);
                          if (editor_state.status().prompt_buffer() ==
                              nullptr) {
                            it->second->status().CopyFrom(
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
        prompt_state->status().set_prompt(options.prompt, buffer);
        insert_mode_options.modify_handler(*buffer);
      });
}

std::unique_ptr<Command> NewLinePromptCommand(
    EditorState& editor_state, wstring description,
    std::function<PromptOptions()> options_supplier) {
  return std::make_unique<LinePromptCommand>(
      editor_state, std::move(description), std::move(options_supplier));
}

}  // namespace afc::editor
