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
    set_bool_variable(variable_atomic_lines(), true);
    set_bool_variable(variable_reload_on_display(), true);
    set_bool_variable(variable_show_in_buffers_list(), false);
    set_bool_variable(variable_push_positions_to_history(), false);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    target->ClearContents(editor_state);
    bool show_in_buffers_list =
        read_bool_variable(variable_show_in_buffers_list());
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
      size_t context_lines_var = static_cast<size_t>(
          max(it.second->read_int_variable(
                  OpenBuffer::variable_buffer_list_context_lines()),
              0));
      auto context = LinesToShow(*it.second, context_lines_var);

      wstring flags = it.second->FlagsString();
      std::shared_ptr<LazyString> name = NewCopyString(
          (context.first == context.second ? L"" : L"╭──") + it.first
          + (flags.empty() ? L"" : L"  ") + flags);
      if (context.first != context.second) {
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
      AdjustLastLine(target, it.second);

      auto start = context.first;
      size_t index = 0;
      while (index < context_lines_var) {
        Line::Options options;
        options.contents =
            NewCopyString(index + 1 == context_lines_var ? L"╰ " : L"│ ");
        if (start < context.second) {
          options.contents =
              StringAppend(options.contents, (*start)->contents());
          options.modifiers.resize(2);
          auto modifiers = (*start)->modifiers();
          options.modifiers.insert(
              options.modifiers.end(), modifiers.begin(), modifiers.end());
          ++start;
        }
        target->AppendRawLine(editor_state, std::make_shared<Line>(options));
        AdjustLastLine(target, it.second);
        ++index;
      }
    }
    editor_state->ScheduleRedraw();
  }

  pair<Tree<std::shared_ptr<Line>>::const_iterator,
       Tree<std::shared_ptr<Line>>::const_iterator>
      LinesToShow(const OpenBuffer& buffer, size_t lines) {
    lines = min(lines, buffer.contents()->size());
    VLOG(5) << buffer.name() << ": Context lines to show: " << lines;
    if (lines == 0) {
      auto last = buffer.contents()->end();
      return make_pair(last, last);
    }
    Tree<std::shared_ptr<Line>>::const_iterator start =
        buffer.contents()->cbegin() + buffer.current_cursor()->line;
    start -= min(buffer.current_cursor()->line,
                 max(lines / 2,
                     lines - min(lines,
                                 static_cast<size_t>(
                                     buffer.contents()->cend() - start))));
    Tree<std::shared_ptr<Line>>::const_iterator stop =
        (static_cast<size_t>(buffer.contents()->end() - start) > lines)
             ? start + lines : buffer.contents()->end();

    // Scroll back if there's a bunch of empty lines.
    while (start > buffer.contents()->cbegin() && (*(stop - 1))->size() == 0) {
      --stop;
      --start;
    }
    CHECK(start <= stop);
    return make_pair(start, stop);
  }

 private:
  void AdjustLastLine(OpenBuffer* target, std::shared_ptr<OpenBuffer> buffer) {
    (*target->contents()->rbegin())->environment()->Define(
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
