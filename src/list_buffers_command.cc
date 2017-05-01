#include "list_buffers_command.h"

#include "char_buffer.h"
#include "command.h"
#include "editor.h"
#include "file_link_mode.h"
#include "insert_mode.h"
#include "lazy_string_append.h"
#include "line_prompt_mode.h"
#include "send_end_of_file_command.h"
#include "wstring.h"

namespace afc {
namespace editor {

namespace {

class ListBuffersBuffer : public OpenBuffer {
 public:
  ListBuffersBuffer(EditorState* editor_state, const wstring& name)
      : OpenBuffer(editor_state, name) {
    editor_state->StartHandlingInterrupts();
    set_bool_variable(variable_atomic_lines(), true);
    set_bool_variable(variable_reload_on_display(), true);
    set_bool_variable(variable_show_in_buffers_list(), false);
    set_bool_variable(variable_push_positions_to_history(), false);
    set_bool_variable(variable_allow_dirty_delete(), true);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    target->ClearContents(editor_state);
    bool show_in_buffers_list =
        read_bool_variable(variable_show_in_buffers_list());

    vector<std::shared_ptr<OpenBuffer>> buffers_to_show;
    for (const auto& it : *editor_state->buffers()) {
      if (!show_in_buffers_list
          && !it.second->read_bool_variable(
                  OpenBuffer::variable_show_in_buffers_list())) {
        LOG(INFO) << "Skipping buffer (!show_in_buffers_list).";
        continue;
      }
      if (it.second.get() == target) {
        LOG(INFO) << "Skipping current buffer.";
        continue;
      }
      buffers_to_show.push_back(it.second);
    }

    sort(buffers_to_show.begin(), buffers_to_show.end(),
         [](const std::shared_ptr<OpenBuffer>& a,
            const std::shared_ptr<OpenBuffer>& b) {
           return a->last_visit() > b->last_visit();
         });
    for (const auto& buffer : buffers_to_show) {
      size_t context_lines_var = static_cast<size_t>(
          max(buffer->read_int_variable(
                  OpenBuffer::variable_buffer_list_context_lines()),
              0));
      auto context = LinesToShow(*buffer, context_lines_var);

      std::shared_ptr<LazyString> name = NewCopyString(buffer->name());
      if (context.first != context.second) {
        name = StringAppend(NewCopyString(L"╭──"), name);
        size_t width =
            target->read_int_variable(OpenBuffer::variable_line_width());
        if (width > name->size()) {
          name = StringAppend(
              name,
              NewCopyString(wstring(width - (name->size() + 1), L'─') + L"╮"));
        }
      }
      if (target->contents()->size() == 1
          && target->contents()->at(0)->size() == 0) {
        target->AppendToLastLine(editor_state, std::move(name));
      } else {
        target->AppendLine(editor_state, std::move(name));
      }
      AdjustLastLine(target, buffer);

      size_t index = 0;
      while (index < context_lines_var) {
        Line::Options options;
        options.contents =
            NewCopyString(index + 1 == context_lines_var ? L"╰ " : L"│ ");
        options.modifiers.resize(options.contents->size());
        if (context.first < context.second) {
          auto line = buffer->LineAt(context.first);
          options.contents = StringAppend(options.contents, line->contents());
          for (const auto& m : line->modifiers()) {
            options.modifiers.push_back(m);
          }
          context.first++;
        }
        CHECK_EQ(options.contents->size(), options.modifiers.size());
        target->AppendRawLine(editor_state, std::make_shared<Line>(options));
        AdjustLastLine(target, buffer);
        ++index;
      }
    }
    editor_state->ScheduleRedraw();
  }

  pair<size_t, size_t> LinesToShow(const OpenBuffer& buffer, size_t lines) {
    lines = min(lines, buffer.contents()->size());
    VLOG(5) << buffer.name() << ": Context lines to show: " << lines;
    if (lines == 0) {
      auto last = buffer.lines_size();
      return make_pair(last, last);
    }
    size_t start = buffer.current_position_line();
    start -= min(start,
                 max(lines / 2,
                     lines - min(lines, buffer.lines_size() - start)));
    size_t stop = min(buffer.lines_size(), start + lines);
    CHECK_LE(start, stop);

    // Scroll back if there's a bunch of empty lines.
    while (start > 0 && buffer.LineAt(stop - 1)->empty()) {
      --stop;
      --start;
    }
    CHECK_LE(start, stop);
    return make_pair(start, stop);
  }

 private:
  void AdjustLastLine(OpenBuffer* target, std::shared_ptr<OpenBuffer> buffer) {
    target->contents()->back()->environment()->Define(
        L"buffer", Value::NewObject(L"Buffer", buffer));
  }
};

class ListBuffersCommand : public Command {
 public:
  const wstring Description() {
    return L"lists all open buffers";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto it = editor_state->buffers()->insert(
        make_pair(OpenBuffer::kBuffersName, nullptr));
    editor_state->set_current_buffer(it.first);
    if (it.second) {
      it.first->second.reset(
          new ListBuffersBuffer(editor_state, OpenBuffer::kBuffersName));
      it.first->second->set_bool_variable(
          OpenBuffer::variable_reload_on_enter(), true);
    }
    editor_state->ResetStatus();
    it.first->second->Reload(editor_state);
    editor_state->PushCurrentPosition();
    editor_state->ScheduleRedraw();
    editor_state->ResetMode();
    editor_state->ResetRepetitions();
  }
};

}  // namespace

std::unique_ptr<Command> NewListBuffersCommand() {
  return std::unique_ptr<Command>(new ListBuffersCommand());
}

}  // namespace afc
}  // namespace editor
