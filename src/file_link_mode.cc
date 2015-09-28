#include "file_link_mode.h"

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <algorithm>
#include <stdexcept>

extern "C" {
#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
}

#include "char_buffer.h"
#include "editor.h"
#include "line_prompt_mode.h"
#include "run_command_handler.h"
#include "search_handler.h"
#include "vm/public/value.h"
#include "wstring.h"

namespace afc {
namespace editor {
namespace {
using std::make_pair;
using std::shared_ptr;
using std::unique_ptr;
using std::sort;

class FileBuffer : public OpenBuffer {
 public:
  FileBuffer(EditorState* editor_state, const wstring& path)
      : OpenBuffer(editor_state, path) {
    set_string_variable(variable_path(), path);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    const wstring path = GetPath();
    const string path_raw = ToByteString(path);
    if (stat(path_raw.c_str(), &stat_buffer_) == -1) {
      return;
    }

    if (target->read_bool_variable(OpenBuffer::variable_clear_on_reload())) {
      target->ClearContents();
    }
    editor_state->ScheduleRedraw();

    if (!S_ISDIR(stat_buffer_.st_mode)) {
      char* tmp = strdup(path_raw.c_str());
      if (0 == strcmp(basename(tmp), "passwd")) {
        RunCommandHandler(L"parsers/passwd <" + path, editor_state);
      } else {
        int fd = open(ToByteString(path).c_str(), O_RDONLY);
        target->SetInputFile(fd, false, -1);
      }
      editor_state->CheckPosition();
      editor_state->PushCurrentPosition();
      return;
    }

    set_bool_variable(variable_atomic_lines(), true);
    target->AppendLine(editor_state, shared_ptr<LazyString>(
        NewCopyString(L"File listing: " + path).release()));

    DIR* dir = opendir(path_raw.c_str());
    assert(dir != nullptr);
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      target->AppendLine(editor_state, shared_ptr<LazyString>(
          NewCopyCharBuffer(FromByteString(entry->d_name).c_str())));
      (*target->contents()->rbegin())->set_activate(
          NewFileLinkMode(editor_state,
              path + L"/" + FromByteString(entry->d_name), false));
    }
    closedir(dir);

    struct Compare {
      bool operator()(const shared_ptr<Line>& a, const shared_ptr<Line>& b) {
        return *a->contents() < *b->contents();
      }
    } compare;

    sort(target->contents()->begin() + 1, target->contents()->end(), compare);
    editor_state->CheckPosition();
    editor_state->PushCurrentPosition();
  }

  void Save(EditorState* editor_state) {
    if (S_ISDIR(stat_buffer_.st_mode)) {
      OpenBuffer::Save(editor_state);
      return;
    }

    const wstring path = GetPath();
    if (!SaveContentsToFile(editor_state, this, path)) {
      return;
    }
    set_modified(false);
    editor_state->SetStatus(L"Saved: " + path);
    for (const auto& dir : editor_state->edge_path()) {
      EvaluateFile(editor_state, dir + L"/hooks/buffer-save.cc");
    }
    for (auto& it : *editor_state->buffers()) {
      if (it.second->read_bool_variable(
              OpenBuffer::variable_reload_on_buffer_write())) {
        it.second->Reload(editor_state);
      }
    }
  }

 private:
  wstring GetPath() const {
    return read_string_variable(variable_path());
  }

  struct stat stat_buffer_;
};

static wstring realpath_safe(const wstring& path) {
  char* result = realpath(ToByteString(path).c_str(), nullptr);
  return result == nullptr ? path : FromByteString(result);
}

wstring GetAnonymousBufferName(size_t i) {
  return L"[anonymous buffer " + std::to_wstring(i) + L"]";
}

class FileLinkMode : public EditorMode {
 public:
  FileLinkMode(const wstring& path, bool ignore_if_not_found)
      : path_(path),
        ignore_if_not_found_(ignore_if_not_found) {}

  void ProcessInput(int c, EditorState* editor_state) {
    switch (c) {
      case '\n':
        {
          OpenFileOptions options;
          options.editor_state = editor_state;
          options.path = path_;
          options.ignore_if_not_found = ignore_if_not_found_;
          afc::editor::OpenFile(options);
          return;
        }

      case 'd':
        {
          wstring path = path_;  // Capture for the lambda.
          vector<wstring> predictions = { L"no", L"yes" };
          unique_ptr<Command> command(NewLinePromptCommand(
              L"unlink " + path_ + L"? [yes/no] ",
              L"confirmation",
              L"Confirmation",
              [path](const wstring input, EditorState* editor_state) {
                if (input == L"yes") {
                  int result = unlink(ToByteString(path).c_str());
                  editor_state->SetStatus(
                      path + L": unlink: "
                      + (result == 0
                         ? L"done"
                         : L"ERROR: " + FromByteString(strerror(errno))));
                } else {
                  // TODO: insert it again?  Actually, only let it be erased
                  // in the other case.
                  editor_state->SetStatus(L"Ignored.");
                }
                editor_state->ResetMode();
              },
              PrecomputedPredictor(predictions)));
          command->ProcessInput('\n', editor_state);
        }
        return;

      default:
        editor_state->SetStatus(
            L"Invalid command: "
            + FromByteString(string(1, static_cast<char>(c))));
    }
  }

  const wstring path_;
  const bool ignore_if_not_found_;
};

static wstring FindPath(
    vector<wstring> search_paths, const wstring& path, vector<int>* positions,
    wstring* pattern) {
  CHECK(!search_paths.empty());

  struct stat dummy;

  // Strip off any trailing colons.
  for (auto search_path_it = search_paths.begin();
       search_path_it != search_paths.end();
       ++search_path_it) {
    for (size_t str_end = path.size();
         str_end != path.npos && str_end != 0;
         str_end = path.find_last_of(':', str_end - 1)) {
      const wstring path_without_suffix =
          *search_path_it + path.substr(0, str_end);
      CHECK(!path_without_suffix.empty());
      if (stat(ToByteString(path_without_suffix).c_str(), &dummy) == -1) {
        continue;
      }

      for (size_t i = 0; i < positions->size(); i++) {
        while (str_end < path.size() && ':' == path[str_end]) {
          str_end++;
        }
        if (str_end == path.size()) { break; }
        size_t next_str_end = path.find(':', str_end);
        const wstring arg = path.substr(str_end, next_str_end);
        if (i == 0 && arg.size() > 0 && arg[0] == '/') {
          *pattern = arg.substr(1);
          break;
        } else {
          try {
            positions->at(i) = stoi(arg);
            if (positions->at(i) > 0) { positions->at(i) --; }
          } catch (const std::invalid_argument& ia) {
            break;
          }
        }
        str_end = next_str_end;
        if (str_end == path.npos) { break; }
      }
      return path_without_suffix;
    }
  }
  return L"";
}

}  // namespace

using std::unique_ptr;

bool SaveContentsToFile(
    EditorState* editor_state, OpenBuffer* buffer, const wstring& path) {
  assert(buffer != nullptr);
  string path_raw = ToByteString(path);
  string tmp_path = path_raw + ".tmp";
  int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if (fd == -1) {
    editor_state->SetStatus(
        FromByteString(tmp_path) + L": open failed: "
        + FromByteString(strerror(errno)));
    return false;
  }
  bool result = SaveContentsToOpenFile(
      editor_state, buffer, FromByteString(tmp_path), fd);
  close(fd);
  if (!result) {
    return false;
  }

  if (rename(tmp_path.c_str(), path_raw.c_str()) == -1) {
    editor_state->SetStatus(
        path + L": rename failed: " + FromByteString(strerror(errno)));
    return false;
  }

  return true;
}

bool SaveContentsToOpenFile(
    EditorState* editor_state, OpenBuffer* buffer, const wstring& path,
    int fd) {
  // TODO: It'd be significant more efficient to do fewer (bigger) writes.
  for (auto it = buffer->contents()->begin(); it != buffer->contents()->end();
       ++it) {
    string str = ToByteString((*it)->contents()->ToString());
    bool include_newline = it + 1 != buffer->contents()->end();
    if (include_newline) {
      str += "\n";
    }
    int write_result = write(fd, str.c_str(), str.size());
    if (write_result == -1) {
      editor_state->SetStatus(
          path + L": write failed: " + std::to_wstring(fd) + L": "
          + FromByteString(strerror(errno)));
      return false;
    }
  }
  return true;
}

shared_ptr<OpenBuffer> GetSearchPathsBuffer(EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.name = L"- search paths";
  auto it = editor_state->buffers()->find(options.name);
  if (it != editor_state->buffers()->end()) {
    return it->second;
  }
  options.path = (*editor_state->edge_path().begin()) + L"/search_paths";
  options.make_current_buffer = false;
  options.use_search_paths = false;
  it = OpenFile(options);
  assert(it != editor_state->buffers()->end());
  assert(it->second != nullptr);
  it->second->set_bool_variable(OpenBuffer::variable_save_on_close(), true);
  if (!editor_state->has_current_buffer()) {
    editor_state->set_current_buffer(it);
    editor_state->ScheduleRedraw();
  }
  return it->second;
}

void GetSearchPaths(EditorState* editor_state, vector<wstring>* output) {
  auto search_paths_buffer = GetSearchPathsBuffer(editor_state);
  if (search_paths_buffer == nullptr) {
    return;
  }
  for (auto it : *search_paths_buffer) {
    output->push_back(it->ToString());
  }
}

map<wstring, shared_ptr<OpenBuffer>>::iterator OpenFile(
    const OpenFileOptions& options) {
  EditorState* editor_state = options.editor_state;
  vector<int> tokens { 0, 0 };
  wstring pattern;
  wstring expanded_path = editor_state->expand_path(options.path);

  vector<wstring> search_paths = { L"" };
  if (options.use_search_paths) {
    GetSearchPaths(editor_state, &search_paths);
  }
  wstring actual_path =
      FindPath(search_paths, expanded_path, &tokens, &pattern);

  if (actual_path.empty()) {
    if (options.ignore_if_not_found) {
      return editor_state->buffers()->end();
    }
    actual_path = expanded_path;
  } else {
    actual_path = realpath_safe(actual_path);
  }

  editor_state->PushCurrentPosition();
  shared_ptr<OpenBuffer> buffer;
  wstring name;
  if (!options.name.empty()) {
    name = options.name;
  } else if (actual_path.empty()) {
    size_t count = 0;
    while (editor_state->buffers()->find(GetAnonymousBufferName(count))
           != editor_state->buffers()->end()) {
      count++;
    }
    name = GetAnonymousBufferName(count);
    buffer.reset(new OpenBuffer(editor_state, name));
  } else {
    name = actual_path;
  }
  auto it = editor_state->buffers()->insert(make_pair(name, buffer));
  if (it.second) {
    if (it.first->second.get() == nullptr) {
      it.first->second.reset(new FileBuffer(editor_state, actual_path));
    }
    it.first->second->Reload(editor_state);
  }
  it.first->second->set_position(LineColumn(tokens[0], tokens[1]));
  if (options.make_current_buffer) {
    editor_state->set_current_buffer(it.first);
    editor_state->ScheduleRedraw();
  }
  SearchHandler(it.first->second->position(), pattern, editor_state);
  return it.first;
}

void OpenAnonymousBuffer(EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  OpenFile(options);
}

unique_ptr<EditorMode> NewFileLinkMode(
    EditorState*, const wstring& path, bool ignore_if_not_found) {
  return std::move(unique_ptr<EditorMode>(
      new FileLinkMode(path, ignore_if_not_found)));
}

}  // namespace afc
}  // namespace editor
