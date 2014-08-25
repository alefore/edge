#include "predictor.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <list>
#include <string>
#include <cstring>

extern "C" {
#include <libgen.h>
#include <sys/types.h>
#include <dirent.h>
}

#include "buffer.h"
#include "editor.h"
#include "predictor.h"

namespace {

using afc::editor::EditorState;
using afc::editor::Line;
using afc::editor::OpenBuffer;
using afc::editor::Predictor;
using std::cout;
using std::function;
using std::shared_ptr;
using std::sort;
using std::string;

class PredictionsBufferImpl : public OpenBuffer {
 public:
  PredictionsBufferImpl(Predictor predictor,
                        const string& input,
                        function<void(string)> consumer)
      : OpenBuffer("- predictions"),
        predictor_(predictor),
        input_(input),
        consumer_(consumer) {}

  void ReloadInto(EditorState* editor_state, OpenBuffer* buffer) {
    predictor_(editor_state, input_, buffer);
  }

 protected:
  void EndOfFile(EditorState* editor_state) {
    OpenBuffer::EndOfFile(editor_state);
    struct Compare {
      bool operator()(const shared_ptr<Line>& a, const shared_ptr<Line>& b) {
        return *a->contents < *b->contents;
      }
    } compare;

    sort(contents()->begin(), contents()->end() - 1, compare);
    if (contents()->size() == 2) {
      consumer_((*contents()->begin())->contents->ToString());
    }
  }

 private:
  Predictor predictor_;
  const string input_;
  std::function<void(string)> consumer_;
};

}  // namespace

namespace afc {
namespace editor {

shared_ptr<OpenBuffer> PredictionsBuffer(
    Predictor predictor, const string& input, function<void(const string&)> consumer) {
  return shared_ptr<OpenBuffer>(
      new PredictionsBufferImpl(predictor, input, consumer));
}

void FilePredictor(EditorState* editor_state,
                   const string& input,
                   OpenBuffer* buffer) {
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

    string basename_prefix;
    string dirname_prefix;
    DIR* dir;
    dir = opendir(input.empty() ? "." : input.c_str());
    if (dir != nullptr) {
      dirname_prefix = input;
    } else {
      char* dirname_copy = strdup(input.c_str());
      dirname_prefix = dirname(dirname_copy);
      free(dirname_copy);
      dir = opendir(dirname_prefix.c_str());
      if (dir == nullptr) { exit(0); }
      if (dirname_prefix == ".") {
        dirname_prefix = "";
      } else if (dirname_prefix != "/") {
        dirname_prefix += "/";
      }

      char* basename_copy = strdup(input.c_str());
      basename_prefix = basename(basename_copy);
      free(basename_copy);
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      string entry_path = entry->d_name;
      if (entry_path.size() < basename_prefix.size()
          || entry_path.substr(0, basename_prefix.size()) != basename_prefix) {
        continue;
      }
      cout << dirname_prefix << entry->d_name << (entry->d_type == DT_DIR ? "/" : "") << "\n";
    }
    closedir(dir);

    exit(0);
  }
  close(pipefd[child_fd]);
  buffer->SetInputFile(pipefd[parent_fd], false, child_pid);
}

void EmptyPredictor(EditorState* editor_state,
                    const string& input,
                    OpenBuffer* buffer) {
  // Pass.
}

}  // namespace afc
}  // namespace editor
