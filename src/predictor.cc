#include "src/predictor.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <list>
#include <memory>
#include <string>

extern "C" {
#include <dirent.h>
#include <libgen.h>
#include <sys/types.h>
}

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/dirname.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/lowercase.h"
#include "src/predictor.h"
#include "src/wstring.h"

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

void HandleEndOfFile(OpenBuffer* buffer,
                     std::function<void(wstring)> consumer) {
  CHECK(buffer != nullptr);

  LOG(INFO) << "Predictions buffer received end of file. Predictions: "
            << buffer->contents()->size();
  buffer->SortContents(
      LineNumber(0), buffer->EndLine(),
      [](const shared_ptr<const Line>& a, const shared_ptr<const Line>& b) {
        return *LowerCase(a->contents()) < *LowerCase(b->contents());
      });

  LOG(INFO) << "Removing duplicates.";
  for (auto line = LineNumber(0);
       line.ToDelta() < buffer->contents()->size();) {
    if (line == LineNumber(0) || buffer->LineAt(line.previous())->ToString() !=
                                     buffer->LineAt(line)->ToString()) {
      line++;
    } else {
      buffer->EraseLines(line, line.next());
    }
  }

  wstring common_prefix = buffer->contents()->front()->contents()->ToString();
  bool results = buffer->contents()->EveryLine(
      [&common_prefix](LineNumber, const Line& line) {
        if (line.empty()) {
          return true;
        }
        VLOG(5) << "Considering prediction: " << line.ToString()
                << " (len: " << line.size() << ")";
        size_t current_size = min(common_prefix.size(), line.size());
        wstring current =
            line.Substring(ColumnNumber(0), ColumnNumberDelta(current_size))
                ->ToString();

        auto prefix_end =
            mismatch(common_prefix.begin(), common_prefix.end(),
                     current.begin(), [](wchar_t common_c, wchar_t current_c) {
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
    consumer(common_prefix);
  } else {
    auto editor_state = buffer->editor();
    auto it =
        editor_state->buffers()->find(buffer->Read(buffer_variables::name));
    if (it == editor_state->buffers()->end()) {
      buffer->status()->SetWarningText(L"Error: predictions buffer not found.");
    } else {
      CHECK_EQ(buffer, it->second.get());
      editor_state->set_current_buffer(it->second);
      buffer->set_current_position_line(LineNumber(0));
      editor_state->ScheduleRedraw();
    }
  }
}
}  // namespace

void Predict(EditorState* editor_state, Predictor predictor, wstring input,
             function<void(const wstring&)> consumer) {
  auto& predictions_buffer =
      (*editor_state->buffers())[PredictionsBufferName()];
  OpenBuffer::Options options;
  options.editor = editor_state;
  options.name = PredictionsBufferName();
  options.generate_contents = [editor_state, predictor, input,
                               consumer](OpenBuffer* buffer) {
    predictor(editor_state, input, buffer);
    buffer->set_current_cursor(LineColumn());
    buffer->AddEndOfFileObserver(
        [buffer, consumer]() { HandleEndOfFile(buffer, consumer); });
  };
  predictions_buffer = std::make_shared<OpenBuffer>(std::move(options));
  predictions_buffer->Set(buffer_variables::show_in_buffers_list, false);
  predictions_buffer->Set(buffer_variables::allow_dirty_delete, true);
  predictions_buffer->Reload();
}

void FilePredictor(EditorState* editor_state, const wstring& input,
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

    wstring resolved_path;
    ResolvePathOptions options;
    options.editor_state = editor_state;
    options.path = path_with_prefix;
    options.output_path = &path_with_prefix;
    if (!ResolvePath(std::move(options))) {
      LOG(INFO) << "Unable to resolve, giving up current search path.";
      continue;
    }

    std::unique_ptr<DIR, std::function<void(DIR*)>> dir(
        opendir(ToByteString(path_with_prefix).c_str()),
        [](DIR* dir) { closedir(dir); });
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
                      entry_path.begin()) ||
          entry_path == "." || entry_path == "..") {
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
      buffer->AppendToLastLine(NewLazyString(FromByteString(prediction)));
      buffer->AppendRawLine(std::make_shared<Line>(Line::Options()));
    }
  }
  buffer->EndOfFile();
}

void EmptyPredictor(EditorState* editor_state, const wstring&,
                    OpenBuffer* buffer) {
  buffer->EndOfFile();
}

namespace {

void RegisterVariations(const wstring& prediction, wchar_t separator,
                        vector<wstring>* output) {
  size_t start = 0;
  DVLOG(5) << "Generating predictions for: " << prediction;
  while (true) {
    start = prediction.find_first_not_of(separator, start);
    if (start == wstring::npos) {
      return;
    }
    output->push_back(prediction.substr(start));
    DVLOG(6) << "Prediction: " << output->back();
    start = prediction.find_first_of(separator, start);
    if (start == wstring::npos) {
      return;
    }
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
      contents->insert(make_pair(variation, NewLazyString(prediction)));
    }
  }
  return [contents](EditorState* editor_state, const wstring& input,
                    OpenBuffer* buffer) {
    for (auto it = contents->lower_bound(input); it != contents->end(); ++it) {
      auto result = mismatch(input.begin(), input.end(), (*it).first.begin());
      if (result.first == input.end()) {
        buffer->AppendToLastLine(it->second);
        buffer->AppendRawLine(std::make_shared<Line>(Line::Options()));
      } else {
        break;
      }
    }
    buffer->EndOfFile();
  };
}

}  // namespace editor
}  // namespace afc
