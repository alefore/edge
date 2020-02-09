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
#include "src/predictor.h"
#include "src/terminal.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {

using std::make_pair;

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

shared_ptr<OpenBuffer> FilterHistory(EditorState* editor_state,
                                     OpenBuffer* history_buffer,
                                     wstring filter) {
  CHECK(!filter.empty());
  CHECK(history_buffer != nullptr);
  auto name = L"- history filter: " +
              history_buffer->Read(buffer_variables::name) + L": " + filter;
  auto element = editor_state->buffers()->insert({name, nullptr}).first;
  if (element->second == nullptr) {
    auto filter_buffer =
        OpenBuffer::New({.editor = editor_state, .name = name});
    filter_buffer->Set(buffer_variables::allow_dirty_delete, true);
    filter_buffer->Set(buffer_variables::show_in_buffers_list, false);
    filter_buffer->Set(buffer_variables::delete_into_paste_buffer, false);
    filter_buffer->Set(buffer_variables::atomic_lines, true);
    filter_buffer->Set(buffer_variables::line_width, 1);

    struct Data {
      // The sum of the line positions at which it occurs. If it
      // only occurs once, it'll be just the position in which it occurred. This
      // is a simple way to try to put more relevant things towards the bottom:
      // things that have been used more frequently and more recently.
      size_t lines_sum = 0;
      size_t match_position = 0;
    };
    std::map<wstring, Data> matches;
    history_buffer->contents()->EveryLine(
        [&](LineNumber position, const Line& line) {
          auto s = line.ToString();
          if (auto match = s.find(filter); match != wstring::npos) {
            auto& output = matches[line.ToString()];
            output.lines_sum += position.line;
            output.match_position = match;
          }
          return true;
        });

    // For sorting.
    std::vector<std::pair<size_t, wstring>> matches_by_lines_sum;
    for (auto& [line_key, data] : matches) {
      matches_by_lines_sum.push_back({data.lines_sum, line_key});
    }
    sort(matches_by_lines_sum.begin(), matches_by_lines_sum.end());

    for (auto& [_, key] : matches_by_lines_sum) {
      const auto match_position = matches[key].match_position;
      Line::Options options;
      options.AppendString(key.substr(0, match_position), {});
      options.AppendString(key.substr(match_position, filter.size()),
                           LineModifierSet{LineModifier::GREEN});
      options.AppendString(key.substr(match_position + filter.size()), {});
      filter_buffer->AppendRawLine(std::make_shared<Line>(std::move(options)));
    }

    element->second = std::move(filter_buffer);
  }
  return element->second;
}

shared_ptr<OpenBuffer> GetPromptBuffer(const PromptOptions& options,
                                       EditorState* editor_state) {
  auto& element =
      *editor_state->buffers()->insert(make_pair(L"- prompt", nullptr)).first;
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
  HistoryScrollBehavior(std::shared_ptr<OpenBuffer> history)
      : history_(std::move(history)) {}

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
        history_->contents()->size() > LineNumberDelta(1)) {
      auto previous_buffer = editor_state->current_buffer();
      editor_state->set_current_buffer(history_);

      LineColumn position = history_->position();
      position.line += delta;
      if (position.line <= LineNumber(0) + history_->contents()->size() &&
          position.line > LineNumber(0)) {
        history_->set_position(position);
      }
      if (history_->current_line() != nullptr) {
        buffer_to_insert->AppendToLastLine(
            history_->current_line()->contents());
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
    auto history = history_;
    if (buffer_->lines_size() > LineNumberDelta(0) &&
        !buffer_->LineAt(LineNumber(0))->empty()) {
      history = FilterHistory(editor_state_, history.get(),
                              buffer_->LineAt(LineNumber(0))->ToString());
    }

    history->set_current_position_line(LineNumber(0) +
                                       history->contents()->size());
    return std::make_unique<HistoryScrollBehavior>(history);
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

    // We make a copy in case cancel_handler or handler delete us.
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
        if (input->size() != ColumnNumberDelta(0)) {
          auto history =
              GetHistoryBuffer(editor_state, options.history_file)->second;
          CHECK(history != nullptr);
          if (history->contents()->size() == LineNumberDelta(0) ||
              (history->contents()->at(history->EndLine())->ToString() !=
               input->ToString())) {
            history->AppendLine(input);
          }
        }
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
