#include "file_link_mode.h"

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

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

void StartDeleteFile(EditorState* editor_state, wstring path) {
  PromptOptions options;
  options.prompt = L"unlink " + path + L"? [yes/no] ",
  options.history_file = L"confirmation";
  options.handler = [path](const wstring input,
                           EditorState* editor_state) {
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
  };
  options.predictor = PrecomputedPredictor({L"no", L"yes"}, '/');
  Prompt(editor_state, std::move(options));
}

class FileBuffer : public OpenBuffer {
 public:
  FileBuffer(EditorState* editor_state, const wstring& path,
             const wstring& name)
      : OpenBuffer(editor_state, name) {
    set_string_variable(variable_path(), path);
  }

  void Enter(EditorState* editor_state) {
    OpenBuffer::Enter(editor_state);

    LOG(INFO) << "Checking if file has changed.";
    const wstring path = GetPath();
    const string path_raw = ToByteString(path);
    struct stat current_stat_buffer;
    if (stat(path_raw.c_str(), &current_stat_buffer) == -1) {
      return;
    }
    if (current_stat_buffer.st_mtime > stat_buffer_.st_mtime) {
      editor_state->SetStatus(L"Underying file has changed!");
    }
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    const wstring path = GetPath();
    const string path_raw = ToByteString(path);
    if (!path.empty() && stat(path_raw.c_str(), &stat_buffer_) == -1) {
      return;
    }

    if (target->read_bool_variable(OpenBuffer::variable_clear_on_reload())) {
      target->ClearContents(editor_state);
    }

    editor_state->ScheduleRedraw();

    if (path.empty()) {
      return;
    }

    if (!S_ISDIR(stat_buffer_.st_mode)) {
      char* tmp = strdup(path_raw.c_str());
      if (0 == strcmp(basename(tmp), "passwd")) {
        RunCommandHandler(L"parsers/passwd <" + path, editor_state);
      } else {
        int fd = open(ToByteString(path).c_str(), O_RDONLY);
        target->SetInputFiles(editor_state, fd, -1, false, -1);
      }
      editor_state->CheckPosition();
      editor_state->PushCurrentPosition();
      return;
    }

    set_bool_variable(variable_atomic_lines(), true);
    target->AppendToLastLine(editor_state,
        NewCopyString(L"File listing: " + path));

    DIR* dir = opendir(path_raw.c_str());
    assert(dir != nullptr);
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      static std::unordered_map<int, wstring> types = {
          { DT_BLK, L" (block dev)" },
          { DT_CHR, L" (char dev)" },
          { DT_DIR, L"/" },
          { DT_FIFO, L" (named pipe)" },
          { DT_LNK, L"@" },
          { DT_REG, L"" },
          { DT_SOCK, L" (unix sock)" }
      };
      if (strcmp(entry->d_name, ".") == 0) {
        continue;  // Showing the link to itself is rather pointless.
      }
      auto type_it = types.find(entry->d_type);
      auto path = FromByteString(entry->d_name);
      target->AppendLine(editor_state, shared_ptr<LazyString>(
          NewCopyString(
              path + (type_it == types.end() ? L"" : type_it->second))));

      {
        unique_ptr<Value> callback(new Value(VMType::FUNCTION));
        // Returns nothing.
        callback->type.type_arguments.push_back(VMType(VMType::VM_VOID));
        callback->callback =
            [editor_state, path](vector<unique_ptr<Value>> args) {
              CHECK_EQ(args.size(), 0);
              StartDeleteFile(editor_state, path);
              return Value::NewVoid();
            };

        (*target->contents()->rbegin())->environment()->Define(
            L"EdgeLineDeleteHandler", std::move(callback));
      }
    }
    closedir(dir);

    list<shared_ptr<Line>> test(
        target->contents()->begin() + 1,
        target->contents()->end());
    target->SortContents(
        target->contents()->begin() + 1,
        target->contents()->end(),
        [](const shared_ptr<Line>& a, const shared_ptr<Line>& b) {
          return *a->contents() < *b->contents();
        });
    editor_state->CheckPosition();
    editor_state->PushCurrentPosition();
  }

  void Save(EditorState* editor_state) {
    const wstring path = GetPath();
    if (path.empty()) {
      OpenBuffer::Save(editor_state);
      editor_state->SetStatus(
          L"Buffer can't be saved: “path” variable is empty.");
      return;
    }
    if (S_ISDIR(stat_buffer_.st_mode)) {
      OpenBuffer::Save(editor_state);
      return;
    }

    if (!SaveContentsToFile(editor_state, this, path)) {
      return;
    }
    set_modified(false);
    editor_state->SetStatus(L"Saved: " + path);
    for (const auto& dir : editor_state->edge_path()) {
      EvaluateFile(editor_state, dir + L"/hooks/buffer-save.cc");
    }
    for (auto& it : *editor_state->buffers()) {
      CHECK(it.second != nullptr);
      if (it.second->read_bool_variable(
              OpenBuffer::variable_reload_on_buffer_write())) {
        it.second->Reload(editor_state);
      }
    }
    stat(ToByteString(path).c_str(), &stat_buffer_);
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

static bool FindPath(
    vector<wstring> search_paths, const wstring& path, wstring* resolved_path,
    vector<int>* positions, wstring* pattern) {
  if (find(search_paths.begin(), search_paths.end(), L"")
          == search_paths.end()) {
    search_paths.push_back(L"");
  }

  struct stat dummy;

  // Strip off any trailing colons.
  for (auto search_path_it = search_paths.begin();
       search_path_it != search_paths.end();
       ++search_path_it) {
    for (size_t str_end = path.size();
         str_end != path.npos && str_end != 0;
         str_end = path.find_last_of(':', str_end - 1)) {
      const wstring path_without_suffix =
          *search_path_it
          + (!search_path_it->empty() && *search_path_it->rbegin() != L'/'
                 ? L"/" : L"")
          + path.substr(0, str_end);
      CHECK(!path_without_suffix.empty());
      VLOG(5) << "Considering path: " << path_without_suffix;
      if (stat(ToByteString(path_without_suffix).c_str(), &dummy) == -1) {
        VLOG(6) << path_without_suffix << ": stat failed";
        continue;
      }
      VLOG(4) << "Stat succeeded: " << path_without_suffix;

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
      *resolved_path = realpath_safe(path_without_suffix);
      VLOG(4) << "Resolved path: " << *resolved_path;
      return true;
    }
  }
  return false;
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
  it->second->set_bool_variable(
      OpenBuffer::variable_show_in_buffers_list(), false);
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
  output->push_back(L"");
  for (auto it : *search_paths_buffer->contents()) {
    if (!it->size() > 0) {
      output->push_back(it->ToString());
    }
  }
}

bool ResolvePath(EditorState* editor_state, const wstring& path,
                 wstring* resolved_path,
                 vector<int>* positions, wstring* pattern) {
  vector<wstring> search_paths = { L"" };
  GetSearchPaths(editor_state, &search_paths);
  *positions = { 0, 0 };
  return FindPath(
      std::move(search_paths), path, resolved_path, positions, pattern);
}

map<wstring, shared_ptr<OpenBuffer>>::iterator OpenFile(
    const OpenFileOptions& options) {
  EditorState* editor_state = options.editor_state;
  vector<int> tokens { 0, 0 };
  wstring pattern;
  wstring expanded_path = editor_state->expand_path(options.path);

  vector<wstring> search_paths = options.initial_search_paths;
  if (options.use_search_paths) {
    GetSearchPaths(editor_state, &search_paths);
  }
  wstring actual_path;
  FindPath(search_paths, expanded_path, &actual_path, &tokens, &pattern);

  if (actual_path.empty()) {
    if (options.ignore_if_not_found) {
      return editor_state->buffers()->end();
    }
    actual_path = expanded_path;
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
    buffer.reset(new FileBuffer(editor_state, actual_path, name));
  } else {
    name = actual_path;
  }
  auto it = editor_state->buffers()->insert(make_pair(name, buffer));
  if (it.second) {
    if (it.first->second.get() == nullptr) {
      it.first->second =
          std::make_shared<FileBuffer>(editor_state, actual_path, actual_path);
    }
    it.first->second->Reload(editor_state);
  }
  it.first->second->set_position(LineColumn(tokens[0], tokens[1]));
  if (options.make_current_buffer) {
    editor_state->set_current_buffer(it.first);
    editor_state->ScheduleRedraw();
  }
  if (!pattern.empty()) {
    SearchOptions search_options;
    search_options.starting_position = it.first->second->position();
    search_options.search_query = pattern;
    JumpToNextMatch(editor_state, search_options);
  }
  return it.first;
}

void OpenAnonymousBuffer(EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  OpenFile(options);
}

}  // namespace afc
}  // namespace editor
