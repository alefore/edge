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
#include "buffer_variables.h"
#include "char_buffer.h"
#include "dirname.h"
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
  PredictionsBufferImpl(EditorState* editor_state,
                        Predictor predictor,
                        const wstring& input,
                        function<void(wstring)> consumer)
      : OpenBuffer(editor_state, PredictionsBufferName()),
        predictor_(predictor),
        input_(input),
        consumer_(consumer) {
    set_bool_variable(buffer_variables::show_in_buffers_list(), false);
    set_bool_variable(buffer_variables::allow_dirty_delete(), true);
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
    SortContents(0, contents()->size() - 1,
        [](const shared_ptr<const Line>& a, const shared_ptr<const Line>& b) {
          return *LowerCase(a->contents()) < *LowerCase(b->contents());
        });
    for (size_t line = 0; line < contents()->size();) {
      if (line == 0
          || LineAt(line - 1)->ToString() != LineAt(line)->ToString()) {
        line++;
      } else {
        EraseLines(line, line + 1);
      }
    }

    wstring common_prefix = contents_.front()->contents()->ToString();
    bool results = contents_.ForEach(
        [&common_prefix](size_t, const Line& line) {
          if (line.empty()) { return true; }
          VLOG(5) << "Considering prediction: " << line.ToString() << " (len: "
                  << line.size() << ")";
          size_t current_size = min(common_prefix.size(), line.size());
          wstring current = line.Substring(0, current_size)->ToString();

          auto prefix_end = mismatch(
              common_prefix.begin(), common_prefix.end(), current.begin(),
              [](wchar_t common_c, wchar_t current_c) {
                return towlower(common_c) == towlower(current_c);
              });
          if (prefix_end.first != common_prefix.end()) {
            if (prefix_end.first == common_prefix.begin()) {
              LOG(INFO) << "Aborting completion.";
              return false;
            }
            common_prefix = wstring(common_prefix.begin(), prefix_end.first);
          }
          return true;
        });
    if (results) {
      consumer_(common_prefix);
    } else {
      auto it = editor_state->buffers()->find(PredictionsBufferName());
      if (it == editor_state->buffers()->end()) {
        editor_state->SetWarningStatus(
            L"Error: predictions buffer not found.");
      } else {
        CHECK_EQ(this, it->second.get());
        it->second->set_current_position_line(0);
        editor_state->set_current_buffer(it);
        editor_state->ScheduleRedraw();
      }
    }
  }

 private:
  Predictor predictor_;
  const wstring input_;
  std::function<void(wstring)> consumer_;
};

}  // namespace

void Predict(
    EditorState* editor_state,
    Predictor predictor,
    wstring input,
    function<void(const wstring&)> consumer) {
  auto& predictions_buffer =
      (*editor_state->buffers())[PredictionsBufferName()];
  predictions_buffer = std::make_shared<PredictionsBufferImpl>(
      editor_state, std::move(predictor), std::move(input),
      std::move(consumer));
  predictions_buffer->Reload(editor_state);
  predictions_buffer->set_current_cursor(LineColumn());
}

void FilePredictor(EditorState* editor_state,
                   const wstring& input,
                   OpenBuffer* buffer) {
  LOG(INFO) << "Generating predictions for: " << input;

  wstring path = editor_state->expand_path(input);
  vector<wstring> search_paths;
  GetSearchPaths(editor_state, &search_paths);

  for (const auto& search_path : search_paths) {
    VLOG(4) << "Considering search path: " << search_path;
    if (!search_path.empty() && !path.empty() && path.front() == '/') {
      VLOG(5) << "Skipping non-empty search path for absolute path.";
      continue;
    }

    wstring path_with_prefix = search_path;
    if (search_path.empty()) {
      path_with_prefix = path.empty() ? L"." : path;
    } else if (!path.empty()) {
      path_with_prefix += L"/" + path;
    }

    string basename_prefix;
    if (path_with_prefix.back() != '/') {
      path_with_prefix = Dirname(path_with_prefix);

      char* path_copy = strdup(ToByteString(path).c_str());
      basename_prefix = basename(path_copy);
      free(path_copy);
    }

    LOG(INFO) << "Reading directory: " << path_with_prefix;
    std::unique_ptr<DIR, decltype(&closedir)> dir(
        opendir(ToByteString(path_with_prefix).c_str()), &closedir);
    if (dir == nullptr) {
      LOG(INFO) << "Unable to open, giving up current search path.";
      continue;
    }

    if (path_with_prefix == L".") {
      path_with_prefix = L"";
    } else if (path_with_prefix.back() != L'/') {
      path_with_prefix += L"/";
    }

    struct dirent* entry;
    while ((entry = readdir(dir.get())) != nullptr) {
      string entry_path = entry->d_name;
      if (!std::equal(basename_prefix.begin(), basename_prefix.end(),
                      entry_path.begin())
          || entry_path == "."
          || entry_path == "..") {
        VLOG(6) << "Skipping entry: " << entry_path;
        continue;
      }
      string prediction = ToByteString(path_with_prefix) + entry->d_name +
          (entry->d_type == DT_DIR ? "/" : "");
      if (!search_path.empty() &&
          std::equal(search_path.begin(), search_path.end(),
                     FromByteString(prediction).begin())) {
        VLOG(6) << "Removing prefix from prediction: " << prediction;
        size_t start = prediction.find_first_not_of('/', search_path.size());
        if (start != prediction.npos) {
          prediction = prediction.substr(start);
        }
      }
      VLOG(5) << "Prediction: " << prediction;
      buffer->AppendToLastLine(editor_state,
                               NewCopyString(FromByteString(prediction)));
      buffer->AppendRawLine(editor_state,
                            std::make_shared<Line>(Line::Options()));
    }
  }
  buffer->EndOfFile(editor_state);
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

const wstring& PredictionsBufferName() {
  static wstring output = L"- predictions";
  return output;
}

Predictor PrecomputedPredictor(const vector<wstring>& predictions,
                               wchar_t separator) {
  const auto contents =
      std::make_shared<multimap<wstring, shared_ptr<LazyString>>>();
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
