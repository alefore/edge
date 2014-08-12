#include "search_handler.h"

#include <iostream>

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include "char_buffer.h"
#include "command_mode.h"
#include "editor.h"

namespace {

using namespace afc::editor;

unique_ptr<LazyString> ReadUntilEnd(int fd) {
  char *buffer = nullptr;
  int buffer_size = 0;
  int buffer_length = 0;
  while (true) {
    if (buffer_size == buffer_length) {
      buffer_size = buffer_size ? buffer_size * 2 : 4096;
      buffer = static_cast<char*>(realloc(buffer, buffer_size));
    }
    int result = read(fd, buffer + buffer_length, buffer_size - buffer_length);
    if (result == 0) {
      buffer = static_cast<char*>(realloc(buffer, buffer_length));
      return NewCharBufferWithOwnership(buffer, buffer_length);
    }
    buffer_length += result;
  }
}

class CommandBuffer : public OpenBuffer {
 public:
  CommandBuffer(const string& command) : command_(command) {}

  void Reload(EditorState* editor_state) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
      exit(1);
    }

    pid_t pid = fork();
    if (pid == 0) {
      close(0);
      close(pipefd[0]);
      if (dup2(pipefd[1], 1) == -1) { exit(1); }
      system(command_.c_str());
      exit(0);
    }
    close(pipefd[1]);
    shared_ptr<LazyString> input(ReadUntilEnd(pipefd[0]).release());
    int status_dummy;
    waitpid(pid, &status_dummy, 0);
    contents_.clear();
    AppendLazyString(input);
    editor_state->screen_needs_redraw = true;
  }

 private:
  const string command_;
};

}  // namespace

namespace afc {
namespace editor {

void RunCommandHandler(const string& input, EditorState* editor_state) {
  if (input.empty()) {
    editor_state->mode = NewCommandMode();
    editor_state->screen_needs_redraw = true;
    return;
  }

  auto it = editor_state->buffers.insert(make_pair("Command: " + input, nullptr));
  editor_state->current_buffer = it.first;
  if (it.second) {
    it.first->second.reset(new CommandBuffer(input));
    it.first->second->Reload(editor_state);
  }
  it.first->second->set_current_position_line(0);
  editor_state->screen_needs_redraw = true;
  editor_state->mode = std::move(NewCommandMode());
}

}  // namespace editor
}  // namespace afc
