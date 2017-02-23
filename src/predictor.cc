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
#include "lowercase.h"
#include "predictor.h"
#include "wstring.h"

namespace afc {
namespace editor {
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
        consumer_(consumer) {
    set_bool_variable(variable_show_in_buffers_list(), false);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* buffer) {
    predictor_(editor_state, input_, buffer);
  }

 protected:
  void EndOfFile(EditorState* editor_state) {
    OpenBuffer::EndOfFile(editor_state);
    LOG(INFO) << "Predictions buffer received end of file. Predictions: "
              << contents()->size();
    if (contents()->empty()) { return; }
    SortContents(contents()->begin(), contents()->end() - 1,
        [](const shared_ptr<Line>& a, const shared_ptr<Line>& b) {
          return *LowerCase(a->contents()) < *LowerCase(b->contents());
        });
    wstring common_prefix =
        LowerCase((*contents()->begin())->contents())->ToString();
    for (auto it = contents()->begin(); it != contents()->end(); ++it) {
      if ((*it)->size() == 0) {
        continue;
      }
      VLOG(5) << "Considering prediction: " << (*it)->ToString() << " (len: "
              << (*it)->size() << ")";
      size_t current_size = min(common_prefix.size(), (*it)->size());
      wstring current =
          LowerCase((*it)->Substring(0, current_size))->ToString();

      auto prefix_end = mismatch(common_prefix.begin(), common_prefix.end(),
                                 current.begin());
      if (prefix_end.first != common_prefix.end()) {
        if (prefix_end.first == common_prefix.begin()) { return; }
        common_prefix = wstring(common_prefix.begin(), prefix_end.first);
      }
    }
    consumer_(common_prefix);
  }

 private:
  Predictor predictor_;
  const wstring input_;
  std::function<void(wstring)> consumer_;
};

}  // namespace

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
  vector<wstring> search_paths;
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

    for (const auto& search_path_it : search_paths) {
      string basename_prefix;
      string dirname_prefix;
      wstring path_with_prefix;
      if (search_path_it.empty()) {
        path_with_prefix = path.empty() ? L"." : path;
      } else {
        path_with_prefix = search_path_it;
        if (!path.empty() && path[0] == '/') {
          VLOG(5) << "Skipping non-empty search path for absolute path.";
          continue;
        }
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
          cout << dirname_prefix << "\n";
          continue;
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
        string prediction = dirname_prefix + entry->d_name +
            (entry->d_type == DT_DIR ? "/" : "");
        if (!search_path_it.empty() &&
            prediction.size() >= search_path_it.size() &&
            prediction.substr(0, search_path_it.size()) == ToByteString(search_path_it)) {
          VLOG(6) << "Removing prefix from prediction: " << prediction;
          size_t start = prediction.find_first_not_of('/', search_path_it.size());
          if (start != prediction.npos) {
            prediction = prediction.substr(start);
          }
        }
        VLOG(5) << "Prediction: " << prediction;
        cout << prediction << "\n";
      }
    }

    exit(0);
  }
  close(pipefd[child_fd]);
  buffer->SetInputFiles(editor_state, pipefd[parent_fd], -1, false, child_pid);
}

void EmptyPredictor(
    EditorState* editor_state, const wstring&, OpenBuffer* buffer) {
  buffer->EndOfFile(editor_state);
}

namespace {

void RegisterVariations(const wstring& prediction, wchar_t separator,
                         vector<wstring>* output) {
  size_t start = 0;
  DVLOG(5) << "Generating predictions for: " << prediction;
  while (true) {
    start = prediction.find_first_not_of(separator, start);
    if (start == wstring::npos) { return; }
    output->push_back(prediction.substr(start));
    DVLOG(6) << "Prediction: " << output->back();
    start = prediction.find_first_of(separator, start);
    if (start == wstring::npos) { return; }
  }
}

}  // namespace

Predictor PrecomputedPredictor(const vector<wstring>& predictions,
                               wchar_t separator) {
  // TODO: Use std::make_shared.
  const shared_ptr<multimap<wstring, shared_ptr<LazyString>>> contents(
      new multimap<wstring, shared_ptr<LazyString>>());
  for (const auto& prediction : predictions) {
    vector<wstring> variations;
    RegisterVariations(prediction, separator, &variations);
    for (auto& variation : variations) {
      contents->insert(make_pair(variation, NewCopyString(prediction)));
    }
  }
  return [contents](EditorState* editor_state, const wstring& input,
                    OpenBuffer* buffer) {
    for (auto it = contents->lower_bound(input); it != contents->end(); ++it) {
      auto result = mismatch(input.begin(), input.end(), (*it).first.begin());
      if (result.first == input.end()) {
        buffer->AppendToLastLine(editor_state, it->second);
        buffer->AppendRawLine(editor_state,
                              std::make_shared<Line>(Line::Options()));
      } else {
        break;
      }
    }
    buffer->EndOfFile(editor_state);
  };
}

}  // namespace afc
}  // namespace editor
