#include "list_buffers_command.h"

#include "char_buffer.h"
#include "command.h"
#include "editor.h"
#include "file_link_mode.h"
#include "line_prompt_mode.h"
#include "send_end_of_file_command.h"
#include "wstring.h"

namespace afc {
namespace editor {

namespace {

class ActivateBufferLineCommand : public EditorMode {
 public:
  ActivateBufferLineCommand(const wstring& name) : name_(name) {}

  void ProcessInput(wint_t c, EditorState* editor_state) {
    switch (c) {
      case 0:  // Send EOF
        {
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) {
            // TODO: Keep a function and re-open the buffer?
            editor_state->SetStatus(L"Buffer not found: " + name_);
            return;
          }
          editor_state->ResetStatus();
          SendEndOfFileToBuffer(editor_state, it->second);
          editor_state->ScheduleRedraw();
          break;
        }
      case '\n':  // Open the current buffer.
        {
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) {
            // TODO: Keep a function and re-open the buffer?
            editor_state->SetStatus(L"Buffer not found: " + name_);
            return;
          }
          editor_state->ResetStatus();
          editor_state->set_current_buffer(it);
          it->second->Enter(editor_state);
          editor_state->PushCurrentPosition();
          editor_state->ScheduleRedraw();
          editor_state->ResetMode();
          break;
        }
      case 'd':  // Delete (close) the current buffer.
        {
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) { return; }
          editor_state->CloseBuffer(it);
          break;
        }
      case 'r':  // Reload the current buffer.
        {
          auto it = editor_state->buffers()->find(name_);
          if (it == editor_state->buffers()->end()) { return; }
          editor_state->SetStatus(L"Reloading buffer: " + name_);
          it->second->Reload(editor_state);
          break;
        }
    }
  }

 private:
  const wstring name_;
};

class ListBuffersBuffer : public OpenBuffer {
 public:
  ListBuffersBuffer(EditorState* editor_state, const wstring& name)
      : OpenBuffer(editor_state, name) {
    set_bool_variable(variable_atomic_lines(), true);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    target->ClearContents(editor_state);
    AppendToLastLine(editor_state, NewCopyString(L"Open Buffers:"));
    for (const auto& it : *editor_state->buffers()) {
      wstring flags = it.second->FlagsString();
      auto name =
          NewCopyString(it.first + (flags.empty() ? L"" : L"  ") + flags);
      target->AppendLine(editor_state, std::move(name));
      (*target->contents()->rbegin())->set_activate(
          unique_ptr<EditorMode>(new ActivateBufferLineCommand(it.first)));
    }
    editor_state->ScheduleRedraw();
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
