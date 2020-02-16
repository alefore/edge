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
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/file_link_mode.h"
#include "src/insert_mode.h"
#include "src/lazy_string_append.h"
#include "src/predictor.h"
#include "src/terminal.h"
#include "src/tokenize.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {

std::multimap<std::wstring, std::shared_ptr<LazyString>> GetCurrentFeatures(
    EditorState* editor) {
  std::multimap<std::wstring, std::shared_ptr<LazyString>> output;
  for (auto& [_, buffer] : *editor->buffers()) {
    // We have to deal with nullptr buffers here because this gets called after
    // the entry for the new buffer has been inserted to the editor, but before
    // the buffer has actually been created.
    if (buffer != nullptr &&
        buffer->Read(buffer_variables::show_in_buffers_list)) {
      output.insert(
          {L"name", NewLazyString(buffer->Read(buffer_variables::name))});
    }
  }
  return output;
}

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

std::optional<
    std::unordered_multimap<std::wstring, std::shared_ptr<LazyString>>>
ParseHistoryLine(EditorState* editor, const std::shared_ptr<LazyString>& line) {
  std::unordered_multimap<std::wstring, std::shared_ptr<LazyString>> output;
  for (const auto& token : TokenizeBySpaces(*line)) {
    auto colon = token.value.find(':');
    if (colon == string::npos) {
      editor->status()->SetWarningText(
          L"Unable to parse prompt line (no colon found in token): " +
          line->ToString());
      return std::nullopt;
    }
    ColumnNumber value_start = token.begin + ColumnNumberDelta(colon);
    ++value_start;  // Skip the colon.
    ColumnNumber value_end = token.end;
    if (value_end == value_start || line->get(value_start) != '\"' ||
        line->get(value_end.previous()) != '\"') {
      editor->status()->SetWarningText(
          L"Unable to parse prompt line (expected quote): " + line->ToString());
      return std::nullopt;
    }
    // Skip quotes:
    ++value_start;
    --value_end;
    output.insert({token.value.substr(0, colon),
                   Substring(line, value_start, value_end - value_start)});
  }
  return output;
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

struct Data {
  // Contains one entry for each execution of the prompt. Each entry
  // contains all the features that were present when the execution
  // happened. We'll use this to establish the proportional probability of
  // each filtered previous value based on the current features.
  std::vector<std::unordered_set<std::wstring>> features;
  std::vector<Token> prompt_tokens;
  LineNumber last_match_line;
};

// Returns the set of keys from `matches`, sorted by their proportional
// probability given the current features (in increasing order).
std::vector<std::wstring> SortMatches(
    std::multimap<std::wstring, std::shared_ptr<LazyString>> features,
    const std::unordered_map<std::wstring, Data>& matches) {
  // Apply naive Bayesian probabilities.
  //
  // Let F = f0, f1, ..., fn be the set of features. We want to compute the
  // probability of m1 given the features: p(mi | F). We know that:
  //
  //     p(mi | F) p(F) = p(mi, F)
  //
  // Since p(F) will be the same for all i (and thus won't affect our
  // computations), we get rid of it.
  //
  //     p(mi | F) ~= p(mi, F)
  //
  // We know that (1)
  //
  //     p(mi, F)
  //   = p(f0, f1, f2, ... fn, mi)
  //   = p(f0 | f1, f2, ..., fn, mi) *
  //     p(f1 | f2, ..., fn, mi) *
  //     ... *
  //     p(fn | mi) *
  //     p(mi)
  //
  // The naive assumption lets us simplify to p(fj | mi) the expression:
  //
  //   p(fj | f(j+1), f(j+2), ... fn, mi)
  //
  // So (1) simplifies to:
  //
  //     p(mi, F)
  //   = p(f0 | mi) * ... * p(fn | mi) * p(mi)
  //   = p(mi) Πj p(fj | mi)
  //
  // Πj denotes the multiplication over all values j.
  //
  // There's a small catch. For features absent from mi's history (that is, for
  // features fj where p(fj|mi) is 0), we don't want to fully discard mi (i.e.,
  // we don't want to assign it a proportional probability of 0). If we did
  // that, sporadic features would be given too much weight. To achieve this, we
  // compute a small value epsilon and use:
  //
  //     p(mi, F) = p(mi) Πj max(epsilon, p(fj | mi))
  using Feature = std::wstring;
  using PromptValue = std::wstring;

  std::unordered_map<PromptValue, double> probability_value;  // p(mi).
  size_t count = 0;
  for (const auto& [prompt_value, data] : matches) {
    count += data.features.size();
  }
  for (const auto& [prompt_value, data] : matches) {
    probability_value[prompt_value] =
        static_cast<double>(data.features.size()) / count;
    VLOG(7) << "Probability for " << prompt_value << ": "
            << probability_value[prompt_value];
  }

  // feature_probability[mi][fj] represents a value p(fj | mi): the probability
  // of feature fj given value mi.
  std::unordered_map<PromptValue, std::unordered_map<Feature, double>>
      feature_probability_given_prompt_value;
  for (const auto& [prompt_value, data] : matches) {
    std::unordered_map<Feature, size_t> feature_count;
    for (const auto& instance : data.features) {
      for (const auto& feature : instance) {
        feature_count[feature]++;
      }
    }
    std::unordered_map<Feature, double>* feature_probability =
        &feature_probability_given_prompt_value[prompt_value];
    for (const auto& [feature, count] : feature_count) {
      feature_probability->insert(
          {feature, static_cast<double>(count) / data.features.size()});
      VLOG(8) << "Probability for " << feature << " given " << prompt_value
              << ": " << feature_probability->find(feature)->second;
    }
  }

  double epsilon = 1.0;
  for (auto& [_, features] : feature_probability_given_prompt_value) {
    for (auto& [_, value] : features) {
      epsilon = min(epsilon, value);
    }
  }
  epsilon /= 2;
  VLOG(5) << "Found epsilon: " << epsilon;

  std::unordered_map<PromptValue, double> current_probability_value;
  for (const auto& [prompt_value, data] : matches) {
    double p = probability_value[prompt_value];
    auto feature_probability =
        feature_probability_given_prompt_value[prompt_value];
    for (const auto& [feature_name, feature_value] : features) {
      auto feature_key =
          feature_name + L":" + QuoteString(feature_value)->ToString();
      if (auto it = feature_probability.find(feature_key);
          it != feature_probability.end()) {
        VLOG(9) << prompt_value << ": Feature " << feature_key
                << " contributes prob: " << it->second;
        p *= it->second;
      } else {
        VLOG(9) << prompt_value << ": Feature " << feature_key
                << " contributes epsilon.";
        p *= epsilon;
      }
    }
    VLOG(6) << "Current probability for " << prompt_value << ": " << p;
    current_probability_value[prompt_value] = p;
  }

  std::vector<std::wstring> output;
  for (const auto& [prompt_value, _] : matches) {
    output.push_back(prompt_value);
  }
  sort(output.begin(), output.end(),
       [&current_probability_value](const std::wstring& a,
                                    const std::wstring& b) {
         return current_probability_value[a] < current_probability_value[b];
       });
  return output;
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

  std::unordered_map<wstring, Data> matches;
  std::vector<Token> filter_tokens = TokenizeBySpaces(*NewLazyString(filter));
  history_buffer->contents()->EveryLine([&](LineNumber position,
                                            const Line& line) {
    auto warn_if = [&](bool condition, wstring description) {
      if (condition) {
        editor_state->status()->SetWarningText(description + L": " +
                                               line.contents()->ToString());
      }
      return condition;
    };
    auto line_keys = ParseHistoryLine(editor_state, line.contents());
    if (!line_keys.has_value()) return true;
    auto range = line_keys.value().equal_range(L"prompt");
    int prompt_count = std::distance(range.first, range.second);
    if (warn_if(prompt_count == 0, L"Line is missing `prompt` section") ||
        warn_if(prompt_count != 1, L"Line has multiple `prompt` sections")) {
      return true;
    }
    auto prompt_value = range.first->second;
    VLOG(8) << "Considering history value: " << prompt_value->ToString();
    std::vector<Token> line_tokens = ExtendTokensToEndOfString(
        prompt_value, TokenizeNameForPrefixSearches(prompt_value));
    Data* output = nullptr;
    if (filter_tokens.empty()) {
      VLOG(6) << "Accepting value (empty filters): " << line.ToString();
      output = &matches[prompt_value->ToString()];
    } else if (auto match = FindFilterPositions(filter_tokens, line_tokens);
               !match.empty()) {
      VLOG(5) << "Accepting value, produced a match: " << line.ToString();
      output = &matches[prompt_value->ToString()];
      output->prompt_tokens = std::move(match);
    }
    if (output == nullptr) return true;
    output->last_match_line = position;
    std::unordered_set<std::wstring> features;
    for (auto& [key, value] : line_keys.value()) {
      if (key != L"prompt") {
        features.insert(key + L":" + QuoteString(value)->ToString());
      }
    }
    output->features.push_back(std::move(features));
    return true;
  });

  VLOG(4) << "Matches found: " << matches.size();

  // For sorting.
  for (auto& key : SortMatches(GetCurrentFeatures(editor_state), matches)) {
    sort(matches[key].prompt_tokens.begin(), matches[key].prompt_tokens.end(),
         [](const Token& a, const Token& b) { return a.begin < b.begin; });
    VLOG(6) << "Producing output: " << key;
    Line::Options options;
    ColumnNumber position;
    auto push_to_position = [&](ColumnNumber end, LineModifierSet modifiers) {
      if (end <= position) return;
      options.AppendString(
          key.substr(position.column, (end - position).column_delta),
          std::move(modifiers));
      position = end;
    };
    for (const auto& token : matches[key].prompt_tokens) {
      push_to_position(token.begin, {});
      push_to_position(token.end, {LineModifier::BOLD});
    }
    push_to_position(ColumnNumber(key.size()), {});
    filter_buffer->AppendRawLine(std::make_shared<Line>(std::move(options)));
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

  insert_mode_options.modify_handler =
      [editor_state, status,
       options](const std::shared_ptr<OpenBuffer>& buffer) {
        return options.change_handler(buffer);
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

                auto buffer_to_insert = OpenBuffer::New(
                    {.editor = editor_state, .name = L"- text inserted"});
                buffer_to_insert->AppendToLastLine(
                    NewLazyString(results.value().common_prefix.value()));
                InsertOptions insert_options;
                insert_options.buffer_to_insert = buffer_to_insert;
                buffer->ApplyToCursors(
                    NewInsertBufferTransformation(std::move(insert_options)));

                options.change_handler(buffer);
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
