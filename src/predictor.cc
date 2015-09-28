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
#include "char_buffer.h"
#include "editor.h"
#include "file_link_mode.h"
#include "predictor.h"
#include "wstring.h"

namespace {

using afc::editor::EditorState;
using afc::editor::Line;
using afc::editor::OpenBuffer;
using afc::editor::Predictor;
using std::cout;
using std::function;
using std::min;
using std::shared_ptr;
using std::sort;
using std::wstring;

class PredictionsBufferImpl : public OpenBuffer {
 public:
  // TODO: Replace "- predictions" with a reference to the right variable.
  PredictionsBufferImpl(EditorState* editor_state,
                        Predictor predictor,
                        const wstring& input,
                        function<void(wstring)> consumer)
      : OpenBuffer(editor_state, L"- predictions"),
        predictor_(predictor),
        input_(input),
        consumer_(consumer) {}

  void ReloadInto(EditorState* editor_state, OpenBuffer* buffer) {
    predictor_(editor_state, input_, buffer);
  }

 protected:
  void EndOfFile(EditorState* editor_state) {
    OpenBuffer::EndOfFile(editor_state);
    if (contents()->empty()) { return; }
    struct Compare {
      bool operator()(const shared_ptr<Line>& a, const shared_ptr<Line>& b) {
        return *a->contents() < *b->contents();
      }
    } compare;

    sort(contents()->begin(), contents()->end(), compare);
    auto it = contents()->begin();
    while (it != contents()->end() && (*it)->size() == 0) {
      ++it;
    }
    if (it == contents()->end()) { return; }
    wstring common_prefix = (*it)->ToString();
    while (it != contents()->end()) {
      if ((*it)->size() == 0) { continue; }
      size_t current_size = min(common_prefix.size(), (*it)->size());
      wstring current = (*it)->Substring(0, current_size)->ToString();

      auto prefix_end = mismatch(common_prefix.begin(), common_prefix.end(),
                                 current.begin());
      if (prefix_end.first != common_prefix.end()) {
        if (prefix_end.first == common_prefix.begin()) { return; }
        common_prefix = wstring(common_prefix.begin(), prefix_end.first);
      }
      ++it;
    }
    consumer_(common_prefix);
  }

 private:
  Predictor predictor_;
  const wstring input_;
  std::function<void(wstring)> consumer_;
};

}  // namespace

namespace afc {
namespace editor {

shared_ptr<OpenBuffer> PredictionsBuffer(
    EditorState* editor_state, Predictor predictor, const wstring& input,
    function<void(const wstring&)> consumer) {
  return shared_ptr<OpenBuffer>(
      new PredictionsBufferImpl(editor_state, predictor, input, consumer));
}

void FilePredictor(EditorState* editor_state,
                   const wstring& input,
                   OpenBuffer* buffer) {
  LOG(INFO) << "Generating predictions for: " << input;

  wstring path = editor_state->expand_path(input);
  vector<wstring> search_paths = { L"" };
  GetSearchPaths(editor_state, &search_paths);

  int pipefd[2];
  static const int parent_fd = 0;
  static const int child_fd = 1;

  if (pipe(pipefd) == -1) { exit(57); }
  pid_t child_pid = fork();
  if (child_pid == -1) {
    editor_state->SetStatus(L"fork failed: " + FromByteString(strerror(errno)));
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
    for (const auto& search_path_it : search_paths) {
      wstring path_with_prefix;
      if (search_path_it.empty()) {
        path_with_prefix = path.empty() ? L"." : path;
      } else {
        path_with_prefix = search_path_it;
        if (!path.empty()) {
          path_with_prefix += L"/" + path;
        }
      }

      std::unique_ptr<DIR, decltype(&closedir)> dir(
          opendir(ToByteString(path_with_prefix).c_str()), &closedir);
      if (dir != nullptr) {
        LOG(INFO) << "Exact match: " << path_with_prefix;
        dirname_prefix = ToByteString(path_with_prefix);
        if (dirname_prefix.back() != '/') {
          dirname_prefix.push_back('/');
        }
      } else {
        char* dirname_copy = strdup(ToByteString(path_with_prefix).c_str());
        dirname_prefix = dirname(dirname_copy);
        free(dirname_copy);
        LOG(INFO) << "Inexact match, trying with dirname: " << dirname_prefix;
        dir.reset(opendir(dirname_prefix.c_str()));
        if (dir == nullptr) {
          LOG(INFO) << "Unable to open, giving up current search path.";
          continue;
        }
        if (dirname_prefix == ".") {
          dirname_prefix = "";
        } else if (dirname_prefix != "/") {
          dirname_prefix += "/";
        }

        char* basename_copy = strdup(ToByteString(path).c_str());
        basename_prefix = basename(basename_copy);
        free(basename_copy);
      }

      CHECK(dir != nullptr);

      struct dirent* entry;
      while ((entry = readdir(dir.get())) != nullptr) {
        string entry_path = entry->d_name;
        if (entry_path.size() < basename_prefix.size()
            || entry_path.substr(0, basename_prefix.size()) != basename_prefix
            || entry_path == "."
            || entry_path == "..") {
          continue;
        }
        cout << dirname_prefix << entry->d_name << (entry->d_type == DT_DIR ? "/" : "") << "\n";
      }
    }

    exit(0);
  }
  close(pipefd[child_fd]);
  buffer->SetInputFile(pipefd[parent_fd], false, child_pid);
}

void EmptyPredictor(
    EditorState* editor_state, const wstring&, OpenBuffer* buffer) {
  buffer->EndOfFile(editor_state);
}

Predictor PrecomputedPredictor(const vector<wstring>& predictions) {
  // TODO: Use std::make_shared.
  const shared_ptr<map<wstring, shared_ptr<LazyString>>> contents(
      new map<wstring, shared_ptr<LazyString>>());
  for (const auto& prediction : predictions) {
    contents->insert(make_pair(prediction, NewCopyString(prediction)));
  }
  return [contents](EditorState* editor_state, const wstring& input,
                    OpenBuffer* buffer) {
    for (auto it = contents->lower_bound(input); it != contents->end(); ++it) {
      auto result = mismatch(input.begin(), input.end(), (*it).first.begin());
      if (result.first == input.end()) {
        buffer->AppendLine(editor_state, it->second);
      } else {
        break;
      }
    }
    buffer->EndOfFile(editor_state);
  };
}

}  // namespace afc
}  // namespace editor
