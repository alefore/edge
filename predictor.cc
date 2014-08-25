#include "predictor.h"

#include <memory>
#include <list>
#include <string>
#include <cstring>

extern "C" {
#include <sys/types.h>
#include <dirent.h>
}

#include "editor.h"

namespace afc {
namespace editor {

void FilePredictor(EditorState* editor_state,
                   const string& input,
                   shared_ptr<OpenBuffer> buffer) {
  int pipefd[2];
  static const int parent_fd = 0;
  static const int child_fd = 1;

  if (pipe(pipefd) == -1) { exit(57); }
  pid_t child_pid = fork();
  if (child_pid == -1) {
    editor_state->SetStatus("fork failed: " + string(strerror(errno)));
    return;
  }
  if (child_pid == 0) {
    close(pipefd[parent_fd]);
    if (dup2(pipefd[child_fd], 1) == -1) { exit(1); }
    if (pipefd[child_fd] != 1) {
      close(pipefd[child_fd]);
    }

    DIR* dir = opendir(input.c_str());
    if (dir == nullptr) { exit(0); }
    struct dirent* entry;
    // TODO: Buffer it.
    while ((entry = readdir(dir)) != nullptr) {
      write(1, entry->d_name, strlen(entry->d_name));
    }
    closedir(dir);

    exit(0);
  }
  close(pipefd[child_fd]);
  buffer->SetInputFile(pipefd[parent_fd], false, child_pid);
}

void EmptyPredictor(EditorState* editor_state,
                    const string& input,
                    shared_ptr<OpenBuffer> buffer) {
  // Pass.
}

}  // namespace afc
}  // namespace editor
