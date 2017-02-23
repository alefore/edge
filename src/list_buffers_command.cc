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

class ActivateBufferLineCommand : public EditorMode {
 public:
  ActivateBufferLineCommand(std::shared_ptr<OpenBuffer>& buffer)
      : buffer_weak_(buffer) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    auto buffer = buffer_weak_.lock();
    switch (c) {
      case 0:  // Send EOF
        {
          if (buffer == nullptr) {
            // TODO: Keep a function and re-open the buffer?
            editor_state->SetStatus(L"Buffer not found");
            return;
          }
          editor_state->ResetStatus();
          SendEndOfFileToBuffer(editor_state, buffer);
          editor_state->ScheduleRedraw();
          break;
        }
      case '\n':  // Open the current buffer.
        {
          if (buffer == nullptr) {
            // TODO: Keep a function and re-open the buffer?
            editor_state->SetStatus(L"Buffer not found");
            return;
          }
          editor_state->ResetStatus();
          auto it = editor_state->buffers()->find(buffer->name());
          if (it == editor_state->buffers()->end()) { return; }
          editor_state->set_current_buffer(it);
          buffer->Enter(editor_state);
          editor_state->PushCurrentPosition();
          editor_state->ScheduleRedraw();
          editor_state->ResetMode();
          break;
        }
      case 'd':  // Delete (close) the current buffer.
        {
          if (buffer == nullptr) { return; }
          auto it = editor_state->buffers()->find(buffer->name());
          if (it == editor_state->buffers()->end()) { return; }
          editor_state->CloseBuffer(it);
          break;
        }
      case 'r':  // Reload the current buffer.
        {
          if (buffer == nullptr) { return; }
          editor_state->SetStatus(L"Reloading buffer: " + buffer->name());
          buffer->Reload(editor_state);
          break;
        }
      case 'w':  // Write the current buffer.
        {
          editor_state->SetStatus(L"Saving buffer: " + buffer->name());
          buffer->Save(editor_state);
          break;
        }
    }
  }

 private:
  const std::weak_ptr<OpenBuffer> buffer_weak_;
};

class ListBuffersBuffer : public OpenBuffer {
 public:
  ListBuffersBuffer(EditorState* editor_state, const wstring& name)
      : OpenBuffer(editor_state, name) {
    set_bool_variable(variable_atomic_lines(), true);
    set_bool_variable(variable_reload_on_display(), true);
    set_bool_variable(variable_show_in_buffers_list(), false);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    target->ClearContents(editor_state);
    AppendToLastLine(editor_state, NewCopyString(L"Buffers:"));
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
      auto context = LinesToShow(*it.second);
      wstring flags = it.second->FlagsString();
      auto name = NewCopyString(
          (context.first == context.second ? L"" : L"╭──") + it.first
          + (flags.empty() ? L"" : L"  ") + flags
          + (context.first == context.second ? L"" : L" ──"));
      target->AppendLine(editor_state, std::move(name));
      AdjustLastLine(editor_state, target, it.second);

      auto start = context.first;
      while (start < context.second) {
        Line::Options options;
        options.contents = StringAppend(
            NewCopyString(start + 1 == context.second ? L"╰ " : L"│ "),
            (*start)->contents());
        options.modifiers.resize(2);
        auto modifiers = (*start)->modifiers();
        options.modifiers.insert(
            options.modifiers.end(), modifiers.begin(), modifiers.end());
        target->AppendRawLine(editor_state, std::make_shared<Line>(options));
        AdjustLastLine(editor_state, target, it.second);
        ++start;
      }
    }
    editor_state->ScheduleRedraw();
  }

  pair<Tree<std::shared_ptr<Line>>::const_iterator,
       Tree<std::shared_ptr<Line>>::const_iterator>
      LinesToShow(const OpenBuffer& buffer) {
    size_t context_lines_var = max(buffer.read_int_variable(
        OpenBuffer::variable_buffer_list_context_lines()), 0);

    size_t lines = min(static_cast<size_t>(context_lines_var),
                       buffer.contents()->size());
    VLOG(5) << buffer.name() << ": Context lines to show: " << lines;
    if (lines == 0) {
      auto last = buffer.contents()->end();
      return make_pair(last, last);
    }
    Tree<std::shared_ptr<Line>>::const_iterator start =
        buffer.current_cursor()->first;
    start -= min(static_cast<size_t>(start - buffer.contents()->cbegin()),
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
  void AdjustLastLine(EditorState* editor_state, OpenBuffer* target,
                      std::shared_ptr<OpenBuffer> buffer) {
    Line& line = *(*target->contents()->rbegin());
    line.set_activate(
        unique_ptr<EditorMode>(new ActivateBufferLineCommand(buffer)));
    line.environment()->Define(L"buffer",
                               Value::NewObject(L"Buffer", buffer));
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
