#include "src/line_prompt_mode.h"

#include <glog/logging.h>

#include <limits>
#include <memory>
#include <string>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command.h"
#include "src/command_mode.h"
#include "src/dirname.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/file_link_mode.h"
#include "src/insert_mode.h"
#include "src/lazy_string_append.h"
#include "src/naive_bayes.h"
#include "src/notification.h"
#include "src/predictor.h"
#include "src/terminal.h"
#include "src/tests/tests.h"
#include "src/tokenize.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {

std::unordered_multimap<std::wstring, std::shared_ptr<LazyString>>
GetCurrentFeatures(EditorState* editor) {
  std::unordered_multimap<std::wstring, std::shared_ptr<LazyString>> output;
  for (auto& [_, buffer] : *editor->buffers()) {
    // We have to deal with nullptr buffers here because this gets called after
    // the entry for the new buffer has been inserted to the editor, but before
    // the buffer has actually been created.
    if (buffer != nullptr &&
        buffer->Read(buffer_variables::show_in_buffers_list) &&
        editor->buffer_tree()->GetBufferIndex(buffer.get()).has_value()) {
      output.insert(
          {L"name", NewLazyString(buffer->Read(buffer_variables::name))});
    }
  }
  editor->ForEachActiveBuffer(
      [&output](const std::shared_ptr<OpenBuffer>& buffer) {
        output.insert(
            {L"active", NewLazyString(buffer->Read(buffer_variables::name))});
        return futures::Past(EmptyValue());
      });
  return output;
}

// Generates additional features that are derived from the features returned by
// GetCurrentFeatures (and thus don't need to be saved).
std::unordered_multimap<std::wstring, std::shared_ptr<LazyString>>
GetSyntheticFeatures(
    const std::unordered_multimap<std::wstring, std::shared_ptr<LazyString>>&
        input) {
  std::unordered_multimap<std::wstring, std::shared_ptr<LazyString>> output;
  std::unordered_set<std::wstring> directories;
  std::unordered_set<std::wstring> extensions;
  for (const auto& [name, value] : input) {
    if (name == L"name") {
      auto value_str = value->ToString();
      if (value_str.find(L'/') != wstring::npos) {
        directories.insert(Dirname(value_str));
      }
      auto extension = SplitExtension(value_str);
      if (extension.suffix.has_value()) {
        extensions.insert(extension.suffix.value().extension);
      }
    }
  }
  for (auto& dir : directories) {
    output.insert({L"directory", NewLazyString(std::move(dir))});
  }
  for (auto& extension : extensions) {
    output.insert({L"extension", NewLazyString(std::move(extension))});
  }
  return output;
}

class GetSyntheticFeaturesTests
    : public tests::TestGroup<GetSyntheticFeaturesTests> {
 public:
  GetSyntheticFeaturesTests() : TestGroup<GetSyntheticFeaturesTests>() {}
  std::wstring Name() const override { return L"GetSyntheticFeaturesTests"; }
  std::vector<tests::Test> Tests() const override {
    return {
        {.name = L"Empty",
         .callback = [] { CHECK_EQ(GetSyntheticFeatures({}).size(), 0ul); }},
        {.name = L"ExtensionsSimple",
         .callback =
             [] {
               std::unordered_multimap<std::wstring,
                                       std::shared_ptr<LazyString>>
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
                                       std::shared_ptr<LazyString>>
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
                                       std::shared_ptr<LazyString>>
                   input;
               input.insert({L"name", NewLazyString(L"/home/alejo/foo.cc")});
               input.insert({L"name", NewLazyString(L"bar.cc")});
               input.insert({L"name", NewLazyString(L"/home/alejo/buffer.h")});
               input.insert({L"name", NewLazyString(L"/home/alejo/README.md")});
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
                                       std::shared_ptr<LazyString>>
                   input;
               input.insert({L"name", NewLazyString(L"foo.cc")});
               auto output = GetSyntheticFeatures(input);
               CHECK_EQ(output.count(L"directory"), 0ul);
             }},
        {.name = L"DirectoryPath",
         .callback =
             [] {
               std::unordered_multimap<std::wstring,
                                       std::shared_ptr<LazyString>>
                   input;
               input.insert(
                   {L"name", NewLazyString(L"/home/alejo/edge/foo.cc")});
               auto output = GetSyntheticFeatures(input);
               CHECK_EQ(output.count(L"directory"), 1ul);
               CHECK(output.find(L"directory")->second->ToString() ==
                     L"/home/alejo/edge");
             }},
        {.name = L"DirectoryMultiple", .callback = [] {
           std::unordered_multimap<std::wstring, std::shared_ptr<LazyString>>
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
         }}};
  }
};

template <>
const bool tests::TestGroup<GetSyntheticFeaturesTests>::registration_ =
    tests::Add<editor::GetSyntheticFeaturesTests>();

map<wstring, shared_ptr<OpenBuffer>>::iterator GetHistoryBuffer(
    EditorState* editor_state, const wstring& name) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.name = L"- history: " + name;
  auto it = editor_state->buffers()->find(options.name);
  if (it != editor_state->buffers()->end()) {
    return it;
  }
  if (!editor_state->edge_path().empty()) {
    options.path =
        (*editor_state->edge_path().begin()) + L"/" + name + L"_history";
  }
  options.insertion_type = BuffersList::AddBufferType::kIgnore;
  it = OpenFile(options);
  CHECK(it != editor_state->buffers()->end());
  CHECK(it->second != nullptr);
  it->second->Set(buffer_variables::save_on_close, true);
  it->second->Set(buffer_variables::trigger_reload_on_buffer_write, false);
  it->second->Set(buffer_variables::show_in_buffers_list, false);
  it->second->Set(buffer_variables::atomic_lines, true);
  if (!editor_state->has_current_buffer()) {
    // Seems lame, but what can we do?
    editor_state->set_current_buffer(it->second);
  }
  return it;
}

ValueOrError<std::unordered_multimap<std::wstring, std::shared_ptr<LazyString>>>
ParseHistoryLine(const std::shared_ptr<LazyString>& line) {
  using Output = ValueOrError<
      std::unordered_multimap<std::wstring, std::shared_ptr<LazyString>>>;
  Output::ValueType output;
  for (const auto& token : TokenizeBySpaces(*line)) {
    auto colon = token.value.find(':');
    if (colon == string::npos) {
      return Output::Error(
          L"Unable to parse prompt line (no colon found in token): " +
          line->ToString());
    }
    ColumnNumber value_start = token.begin + ColumnNumberDelta(colon);
    ++value_start;  // Skip the colon.
    ColumnNumber value_end = token.end;
    if (value_end == value_start || line->get(value_start) != '\"' ||
        line->get(value_end.previous()) != '\"') {
      return Output::Error(L"Unable to parse prompt line (expected quote): " +
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
  return Output::Value(std::move(output));
}

std::shared_ptr<LazyString> QuoteString(std::shared_ptr<LazyString> src) {
  std::vector<std::shared_ptr<LazyString>> output;
  ColumnNumber begin;
  while (begin.ToDelta() < src->size()) {
    auto end = begin;
    while (end.ToDelta() < src->size() && src->get(end) != L'\"' &&
           src->get(end) != L'\\') {
      ++end;
    }
    if (begin < end) {
      output.emplace_back(Substring(src, begin, end - begin));
    }
    begin = end;
    if (begin.ToDelta() < src->size()) {
      CHECK(src->get(begin) == L'\\' || src->get(begin) == L'\"');
      output.emplace_back(NewLazyString(std::wstring{L'\\', src->get(begin)}));
      ++begin;
    }
  }
  return StringAppend(NewLazyString(L"\""), Concatenate(output),
                      NewLazyString(L"\""));
}

std::shared_ptr<LazyString> BuildHistoryLine(
    EditorState* editor, std::shared_ptr<LazyString> input) {
  std::vector<std::shared_ptr<LazyString>> line_for_history;
  line_for_history.emplace_back(NewLazyString(L"prompt:"));
  line_for_history.emplace_back(QuoteString(std::move(input)));
  for (auto& [name, feature] : GetCurrentFeatures(editor)) {
    line_for_history.emplace_back(NewLazyString(L" " + name + L":"));
    line_for_history.emplace_back(QuoteString(feature));
  }
  return Concatenate(std::move(line_for_history));
}

void AddLineToHistory(EditorState* editor, std::wstring history_file,
                      std::shared_ptr<LazyString> input) {
  if (input->size().IsZero()) return;
  auto history = GetHistoryBuffer(editor, history_file)->second;
  CHECK(history != nullptr);
  auto history_line = BuildHistoryLine(editor, input);
  CHECK(history_line != nullptr);
  history->AppendLine(history_line);
}

std::shared_ptr<Line> ColorizeLine(std::shared_ptr<LazyString> line,
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
  return std::make_shared<Line>(std::move(options));
}

std::shared_ptr<OpenBuffer> FilterHistory(EditorState* editor_state,
                                          OpenBuffer* history_buffer,
                                          std::wstring filter) {
  CHECK(history_buffer != nullptr);
  auto name = L"- history filter: " +
              history_buffer->Read(buffer_variables::name) + L": " + filter;
  auto element = editor_state->buffers()->insert({name, nullptr}).first;
  auto filter_buffer = OpenBuffer::New({.editor = editor_state, .name = name});
  filter_buffer->Set(buffer_variables::allow_dirty_delete, true);
  filter_buffer->Set(buffer_variables::show_in_buffers_list, false);
  filter_buffer->Set(buffer_variables::delete_into_paste_buffer, false);
  filter_buffer->Set(buffer_variables::atomic_lines, true);
  filter_buffer->Set(buffer_variables::line_width, 1);

  std::unordered_map<std::wstring, std::vector<naive_bayes::FeaturesSet>>
      history_data;
  std::unordered_map<std::wstring, std::vector<Token>> history_prompt_tokens;
  std::vector<Token> filter_tokens = TokenizeBySpaces(*NewLazyString(filter));
  history_buffer->contents()->EveryLine([&](LineNumber, const Line& line) {
    auto warn_if = [&](bool condition, wstring description) {
      if (condition) {
        editor_state->status()->SetWarningText(description + L": " +
                                               line.contents()->ToString());
      }
      return condition;
    };
    auto line_keys = ParseHistoryLine(line.contents());
    if (line_keys.IsError()) {
      editor_state->status()->SetWarningText(line_keys.error.value());
      return true;
    }
    auto range = line_keys.value.value().equal_range(L"prompt");
    int prompt_count = std::distance(range.first, range.second);
    if (warn_if(prompt_count == 0, L"Line is missing `prompt` section") ||
        warn_if(prompt_count != 1, L"Line has multiple `prompt` sections")) {
      return true;
    }
    auto prompt_value = range.first->second;
    VLOG(8) << "Considering history value: " << prompt_value->ToString();
    std::vector<Token> line_tokens = ExtendTokensToEndOfString(
        prompt_value, TokenizeNameForPrefixSearches(prompt_value));
    auto event_key = prompt_value->ToString();
    std::vector<naive_bayes::FeaturesSet>* output = nullptr;
    if (filter_tokens.empty()) {
      VLOG(6) << "Accepting value (empty filters): " << line.ToString();
      output = &history_data[event_key];
    } else if (auto match = FindFilterPositions(filter_tokens, line_tokens);
               !match.empty()) {
      VLOG(5) << "Accepting value, produced a match: " << line.ToString();
      output = &history_data[event_key];
      history_prompt_tokens.insert({event_key, std::move(match)});
    } else {
      return true;
    }
    std::unordered_set<std::wstring> features;
    for (auto& [key, value] : line_keys.value.value()) {
      if (key != L"prompt") {
        features.insert(key + L":" + QuoteString(value)->ToString());
      }
    }
    output->push_back(std::move(features));
    return true;
  });

  VLOG(4) << "Matches found: " << history_data.size();

  // For sorting.
  auto features = GetCurrentFeatures(editor_state);
  auto synthetic_features = GetSyntheticFeatures(features);
  features.insert(synthetic_features.begin(), synthetic_features.end());

  std::unordered_set<std::wstring> current_features;
  for (const auto& [name, value] : features) {
    current_features.insert(name + L":" + QuoteString(value)->ToString());
  }

  for (auto& key : naive_bayes::Sort(history_data, current_features)) {
    std::vector<TokenAndModifiers> tokens;
    for (auto token : history_prompt_tokens[key]) {
      tokens.push_back({token, LineModifierSet{LineModifier::BOLD}});
    }
    filter_buffer->AppendRawLine(
        ColorizeLine(NewLazyString(key), std::move(tokens)));
  }

  if (filter_buffer->lines_size() > LineNumberDelta(1)) {
    VLOG(5) << "Erasing the first (empty) line.";
    CHECK(filter_buffer->LineAt(LineNumber())->empty());
    filter_buffer->EraseLines(LineNumber(), LineNumber().next());
  }

  element->second = std::move(filter_buffer);
  return element->second;
}

shared_ptr<OpenBuffer> GetPromptBuffer(const PromptOptions& options,
                                       EditorState* editor_state) {
  auto& element =
      *editor_state->buffers()->insert({L"- prompt", nullptr}).first;
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
    CHECK(element.second->contents()->back()->empty());
  }
  element.second->Set(buffer_variables::contents_type,
                      options.prompt_contents_type);
  element.second->Reload();
  return element.second;
}

class HistoryScrollBehavior : public ScrollBehavior {
 public:
  HistoryScrollBehavior(std::shared_ptr<OpenBuffer> history,
                        std::shared_ptr<LazyString> original_input,
                        Status* status)
      : history_(std::move(history)),
        original_input_(std::move(original_input)),
        status_(status),
        previous_prompt_context_(status->prompt_context()) {
    CHECK(original_input_ != nullptr);
    CHECK(status_ != nullptr);
    CHECK(status->GetType() == Status::Type::kPrompt);
  }

  void Up(EditorState* editor_state, OpenBuffer* buffer) override {
    ScrollHistory(editor_state, buffer, LineNumberDelta(-1));
  }

  void Down(EditorState* editor_state, OpenBuffer* buffer) override {
    ScrollHistory(editor_state, buffer, LineNumberDelta(+1));
  }

  void Left(EditorState* editor_state, OpenBuffer* buffer) override {
    DefaultScrollBehavior().Left(editor_state, buffer);
  }

  void Right(EditorState* editor_state, OpenBuffer* buffer) override {
    DefaultScrollBehavior().Right(editor_state, buffer);
  }

  void Begin(EditorState* editor_state, OpenBuffer* buffer) override {
    DefaultScrollBehavior().Begin(editor_state, buffer);
  }

  void End(EditorState* editor_state, OpenBuffer* buffer) override {
    DefaultScrollBehavior().End(editor_state, buffer);
  }

 private:
  void ScrollHistory(EditorState* editor_state, OpenBuffer* buffer,
                     LineNumberDelta delta) const {
    auto buffer_to_insert =
        OpenBuffer::New({.editor = editor_state, .name = L"- text inserted"});

    if (history_ != nullptr &&
        (history_->contents()->size() > LineNumberDelta(1) ||
         !history_->LineAt(LineNumber())->empty())) {
      LineColumn position = history_->position();
      position.line = min(position.line.PlusHandlingOverflow(delta),
                          LineNumber() + history_->contents()->size());
      history_->set_position(position);
      if (position.line < LineNumber(0) + history_->contents()->size()) {
        status_->set_prompt_context(history_);
        if (history_->current_line() != nullptr) {
          buffer_to_insert->AppendToLastLine(
              history_->current_line()->contents());
        }
      } else {
        status_->set_prompt_context(previous_prompt_context_);
        buffer_to_insert->AppendToLastLine(original_input_);
      }
    }

    DeleteOptions delete_options;
    delete_options.modifiers.paste_buffer_behavior =
        Modifiers::PasteBufferBehavior::kDoNothing;
    delete_options.modifiers.structure = StructureLine();
    delete_options.modifiers.boundary_begin = Modifiers::LIMIT_CURRENT;
    delete_options.modifiers.boundary_end = Modifiers::LIMIT_CURRENT;
    buffer->ApplyToCursors(NewDeleteTransformation(delete_options));
    InsertOptions insert_options;
    insert_options.buffer_to_insert = std::move(buffer_to_insert);
    buffer->ApplyToCursors(
        NewInsertBufferTransformation(std::move(insert_options)));
  }

  const std::shared_ptr<OpenBuffer> history_;
  const std::shared_ptr<LazyString> original_input_;
  Status* const status_;
  const std::shared_ptr<OpenBuffer> previous_prompt_context_;
};

class HistoryScrollBehaviorFactory : public ScrollBehaviorFactory {
 public:
  HistoryScrollBehaviorFactory(EditorState* editor_state, wstring prompt,
                               std::shared_ptr<OpenBuffer> history,
                               Status* status,
                               std::shared_ptr<OpenBuffer> buffer)
      : editor_state_(editor_state),
        prompt_(std::move(prompt)),
        history_(std::move(history)),
        status_(status),
        buffer_(std::move(buffer)) {}

  std::unique_ptr<ScrollBehavior> Build() override {
    CHECK_GT(buffer_->lines_size(), LineNumberDelta(0));
    auto input = buffer_->LineAt(LineNumber(0))->contents();
    auto history =
        FilterHistory(editor_state_, history_.get(), input->ToString());
    history->set_current_position_line(LineNumber(0) +
                                       history->contents()->size());
    return std::make_unique<HistoryScrollBehavior>(history, input, status_);
  }

 private:
  EditorState* const editor_state_;
  const wstring prompt_;
  const std::shared_ptr<OpenBuffer> history_;
  Status* const status_;
  const std::shared_ptr<OpenBuffer> buffer_;
};

class LinePromptCommand : public Command {
 public:
  LinePromptCommand(wstring description,
                    std::function<PromptOptions(EditorState*)> options)
      : description_(std::move(description)), options_(std::move(options)) {}

  wstring Description() const override { return description_; }
  wstring Category() const override { return L"Prompt"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    Prompt(options_(editor_state));
  }

 private:
  const wstring description_;
  std::function<PromptOptions(EditorState*)> options_;
};

// status_buffer is the buffer with the contents of the prompt. tokens_future is
// received as a future so that we can detect if the prompt input changes
// between the time when `ColorizePrompt` is executed and the time when the
// tokens become available.
futures::Value<EmptyValue> ColorizePrompt(
    std::shared_ptr<OpenBuffer> status_buffer, Status* status,
    int status_version, std::shared_ptr<Notification> abort_notification,
    futures::Value<ColorizePromptOptions> options_future) {
  CHECK(status_buffer != nullptr);
  CHECK(status);
  CHECK_EQ(status_buffer->lines_size(), LineNumberDelta(1));
  auto original_line = status_buffer->LineAt(LineNumber(0));

  return futures::Transform(options_future, [status_buffer, status,
                                             status_version, abort_notification,
                                             original_line](
                                                ColorizePromptOptions options) {
    if (status->GetType() != Status::Type::kPrompt) {
      LOG(INFO) << "Status is no longer a prompt, aborting colorize prompt.";
      return futures::Past(EmptyValue());
    }

    if (status->prompt_buffer() != status_buffer) {
      LOG(INFO) << "Prompt buffer has changed, aborting colorize prompt.";
      return futures::Past(EmptyValue());
    }
    if (abort_notification->HasBeenNotified()) {
      LOG(INFO) << "Abort notification notified, aborting colorize prompt.";
      return futures::Past(EmptyValue());
    }

    CHECK_EQ(status_buffer->lines_size(), LineNumberDelta(1));
    auto line = status_buffer->LineAt(LineNumber(0));
    if (original_line->ToString() != line->ToString()) {
      LOG(INFO) << "Line has changed, ignoring prompt colorize update.";
      return futures::Past(EmptyValue());
    }

    status_buffer->AppendRawLine(
        ColorizeLine(line->contents(), std::move(options.tokens)));
    status_buffer->EraseLines(LineNumber(0), LineNumber(1));
    if (options.context.has_value()) {
      status->set_prompt_context(options.context.value());
    }
    status->prompt_extra_information()->MarkVersionDone(status_version);
    return futures::Past(EmptyValue());
  });
}
}  // namespace

using std::shared_ptr;
using std::unique_ptr;

void Prompt(PromptOptions options) {
  CHECK(options.handler);
  auto editor_state = options.editor_state;
  CHECK(editor_state != nullptr);
  auto history = GetHistoryBuffer(editor_state, options.history_file)->second;
  history->set_current_position_line(LineNumber(0) +
                                     history->contents()->size());

  auto buffer = GetPromptBuffer(options, editor_state);
  CHECK(buffer != nullptr);

  auto active_buffers = editor_state->active_buffers();
  auto status = options.status == PromptOptions::Status::kEditor ||
                        active_buffers.size() != 1
                    ? editor_state->status()
                    : active_buffers[0]->status();

  CHECK(status != nullptr);

  Modifiers original_modifiers = editor_state->modifiers();
  editor_state->set_modifiers(Modifiers());

  {
    auto buffer_to_insert =
        OpenBuffer::New({.editor = editor_state, .name = L"- text inserted"});
    buffer_to_insert->AppendToLastLine(
        NewLazyString(std::move(options.initial_value)));
    InsertOptions insert_options;
    insert_options.buffer_to_insert = std::move(buffer_to_insert);
    buffer->ApplyToCursors(
        NewInsertBufferTransformation(std::move(insert_options)));
  }

  InsertModeOptions insert_mode_options;
  insert_mode_options.editor_state = editor_state;
  insert_mode_options.buffers = {buffer};

  // Notification that can be used to abort an ongoing execution of
  // `colorize_options_provider`. Every time we call `colorize_options_provider`
  // from modify_handler, we notify the previous notification and set this to a
  // new notification that will be given to the `colorize_options_provider`.
  auto abort_notification_ptr = std::make_shared<std::shared_ptr<Notification>>(
      std::make_shared<Notification>());
  insert_mode_options.modify_handler =
      [editor_state, history, status, options,
       abort_notification_ptr](const std::shared_ptr<OpenBuffer>& buffer) {
        auto line = buffer->LineAt(LineNumber())->contents();
        int status_version =
            status->prompt_extra_information()->StartNewVersion();
        if (options.colorize_options_provider == nullptr) {
          return futures::Past(EmptyValue());
        }
        auto progress_channel = std::make_unique<ProgressChannel>(
            buffer->work_queue(),
            [status, status_version](ProgressInformation extra_information) {
              if (status->GetType() != Status::Type::kPrompt) return;
              CHECK(status->prompt_extra_information() != nullptr);
              for (const auto& [key, value] : extra_information.values) {
                status->prompt_extra_information()->SetValue(
                    key, status_version, value);
              }
            },
            WorkQueueChannelConsumeMode::kLastAvailable);
        (*abort_notification_ptr)->Notify();
        *abort_notification_ptr = std::make_shared<Notification>();
        return ColorizePrompt(
            buffer, status, status_version, *abort_notification_ptr,
            options.colorize_options_provider(line, std::move(progress_channel),
                                              *abort_notification_ptr));
      };

  insert_mode_options.scroll_behavior =
      std::make_unique<HistoryScrollBehaviorFactory>(
          editor_state, options.prompt, history, status, buffer);

  insert_mode_options.escape_handler = [editor_state, options, status,
                                        original_modifiers]() {
    LOG(INFO) << "Running escape_handler from Prompt.";
    editor_state->set_modifiers(original_modifiers);
    status->Reset();

    if (options.cancel_handler) {
      VLOG(5) << "Running cancel handler.";
      options.cancel_handler(editor_state);
    } else {
      VLOG(5) << "Running handler on empty input.";
      options.handler(L"", editor_state);
    }
    editor_state->set_keyboard_redirect(nullptr);
  };

  insert_mode_options.new_line_handler =
      [editor_state, options, status,
       original_modifiers](const std::shared_ptr<OpenBuffer>& buffer) {
        auto input = buffer->current_line()->contents();
        AddLineToHistory(buffer->editor(), options.history_file, input);
        auto ensure_survival_of_current_closure =
            editor_state->keyboard_redirect();
        editor_state->set_keyboard_redirect(nullptr);
        status->Reset();
        editor_state->set_modifiers(original_modifiers);
        return options.handler(input->ToString(), editor_state);
      };

  insert_mode_options.start_completion =
      [editor_state, options,
       status](const std::shared_ptr<OpenBuffer>& buffer) {
        auto input = buffer->current_line()->contents()->ToString();
        LOG(INFO) << "Triggering predictions from: " << input;
        PredictOptions predict_options;
        predict_options.editor_state = editor_state;
        predict_options.predictor = options.predictor;
        predict_options.source_buffers = options.source_buffers;
        predict_options.input_buffer = buffer;
        predict_options.input_selection_structure = StructureLine();
        predict_options.status = status;

        CHECK(status->prompt_extra_information() != nullptr);
        Predict(std::move(predict_options))
            .SetConsumer([editor_state, options, buffer, status,
                          input](std::optional<PredictResults> results) {
              if (!results.has_value()) return;
              if (results.value().common_prefix.has_value() &&
                  !results.value().common_prefix.value().empty() &&
                  input != results.value().common_prefix.value()) {
                LOG(INFO) << "Prediction advanced from " << input << " to "
                          << results.value();

                DeleteOptions delete_options;
                delete_options.modifiers.paste_buffer_behavior =
                    Modifiers::PasteBufferBehavior::kDoNothing;
                delete_options.modifiers.structure = StructureLine();
                delete_options.modifiers.boundary_begin =
                    Modifiers::LIMIT_CURRENT;
                delete_options.modifiers.boundary_end =
                    Modifiers::LIMIT_CURRENT;
                buffer->ApplyToCursors(NewDeleteTransformation(delete_options));

                std::shared_ptr<LazyString> line =
                    NewLazyString(results.value().common_prefix.value());
                auto buffer_to_insert = OpenBuffer::New(
                    {.editor = editor_state, .name = L"- text inserted"});
                buffer_to_insert->AppendToLastLine(line);
                InsertOptions insert_options;
                insert_options.buffer_to_insert = buffer_to_insert;
                buffer->ApplyToCursors(
                    NewInsertBufferTransformation(std::move(insert_options)));

                if (options.colorize_options_provider != nullptr) {
                  int status_version =
                      status->prompt_extra_information()->StartNewVersion();
                  ColorizePrompt(
                      buffer, status, status_version,
                      std::make_shared<Notification>(),
                      options.colorize_options_provider(
                          line,
                          std::make_unique<ProgressChannel>(
                              buffer->work_queue(),
                              [](ProgressInformation) {
                                /* Nothing for now. */
                              },
                              WorkQueueChannelConsumeMode::kLastAvailable),
                          std::make_shared<Notification>()));
                }
              } else {
                LOG(INFO) << "Prediction didn't advance.";
                auto buffers = editor_state->buffers();
                auto name = PredictionsBufferName();
                if (auto it = buffers->find(name); it != buffers->end()) {
                  it->second->set_current_position_line(LineNumber(0));
                  editor_state->set_current_buffer(it->second);
                  if (editor_state->status()->prompt_buffer() == nullptr) {
                    it->second->status()->CopyFrom(*status);
                  }
                } else {
                  editor_state->status()->SetWarningText(
                      L"Error: Predict: predictions buffer not found: " + name);
                }
              }
            });
        return true;
      };

  EnterInsertMode(insert_mode_options);
  // We do this after `EnterInsertMode` because `EnterInsertMode` resets the
  // status.
  status->set_prompt(options.prompt, buffer);
  insert_mode_options.modify_handler(buffer);
}

std::unique_ptr<Command> NewLinePromptCommand(
    wstring description, std::function<PromptOptions(EditorState*)> options) {
  return std::make_unique<LinePromptCommand>(std::move(description),
                                             std::move(options));
}

}  // namespace editor
}  // namespace afc
