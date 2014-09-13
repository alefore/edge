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
#include "file_link_mode.h"
#include "editor.h"
#include "line_prompt_mode.h"
#include "run_command_handler.h"
#include "search_handler.h"

namespace {
using std::make_pair;
using std::shared_ptr;
using std::to_string;
using std::unique_ptr;
using std::sort;
using namespace afc::editor;

class FileBuffer : public OpenBuffer {
 public:
  FileBuffer(EditorState* editor_state, const string& path)
      : OpenBuffer(editor_state, path) {
    set_string_variable(variable_path(), path);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    const string& path = read_string_variable(variable_path());
    if (stat(path.c_str(), &stat_buffer_) == -1) {
      return;
    }

    if (target->read_bool_variable(OpenBuffer::variable_clear_on_reload())) {
      target->ClearContents();
    }
    editor_state->ScheduleRedraw();

    if (!S_ISDIR(stat_buffer_.st_mode)) {
      char* tmp = strdup(path.c_str());
      if (0 == strcmp(basename(tmp), "passwd")) {
        RunCommandHandler("parsers/passwd <" + path, editor_state);
      } else {
        LoadMemoryMappedFile(editor_state, path, target);
      }
      editor_state->CheckPosition();
      editor_state->PushCurrentPosition();
      return;
    }

    set_bool_variable(variable_atomic_lines(), true);
    target->AppendLine(editor_state, shared_ptr<LazyString>(
        NewCopyString("File listing: " + path).release()));

    DIR* dir = opendir(path.c_str());
    assert(dir != nullptr);
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      target->AppendLine(editor_state, shared_ptr<LazyString>(
          NewCopyCharBuffer(entry->d_name).release()));
      (*target->contents()->rbegin())->set_activate(
          NewFileLinkMode(editor_state, path + "/" + entry->d_name, false));
    }
    closedir(dir);

    struct Compare {
      bool operator()(const shared_ptr<Line>& a, const shared_ptr<Line>& b) {
        return *a->contents() < *b->contents();
      }
    } compare;

    sort(target->contents()->begin() + 1, target->contents()->end() - 1, compare);
    editor_state->CheckPosition();
    editor_state->PushCurrentPosition();
  }

  void Save(EditorState* editor_state) {
    if (S_ISDIR(stat_buffer_.st_mode)) {
      OpenBuffer::Save(editor_state);
      return;
    }

    const string& path = read_string_variable(variable_path());
    if (SaveContentsToFile(editor_state, this, path)) {
      set_modified(false);
      editor_state->SetStatus("Saved: " + path);
    }
  }

 private:
  struct stat stat_buffer_;
};

static string realpath_safe(const string& path) {
  char* result = realpath(path.c_str(), nullptr);
  return result == nullptr ? path : string(result);
}

string GetAnonymousBufferName(size_t i) {
  return "[anonymous buffer " + to_string(i) + "]";
}

class FileLinkMode : public EditorMode {
 public:
  FileLinkMode(const string& path, bool ignore_if_not_found)
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
          string path = path_;  // Capture for the lambda.
          vector<string> predictions = { "no", "yes" };
          unique_ptr<Command> command(NewLinePromptCommand(
              "unlink " + path_ + "? [yes/no] ",
              "confirmation",
              "Confirmation",
              [path](const string input, EditorState* editor_state) {
                if (input == "yes") {
                  int result = unlink(path.c_str());
                  editor_state->SetStatus(
                      path + ": unlink: "
                      + (result == 0 ? "done" : "ERROR: " + string(strerror(errno))));
                } else {
                  // TODO: insert it again?  Actually, only let it be erased
                  // in the other case.
                  editor_state->SetStatus("Ignored.");
                }
                editor_state->ResetMode();
              },
              PrecomputedPredictor(predictions)));
          command->ProcessInput('\n', editor_state);
        }
        return;

      default:
        editor_state->SetStatus("Invalid command: " + string(1, static_cast<char>(c)));
    }
  }

  const string path_;
  const bool ignore_if_not_found_;
};

static string FindPath(
    const string& path, vector<int>* positions, string* pattern) {
  struct stat dummy;

  // Strip off any trailing colons.
  for (size_t str_end = path.size();
       str_end != path.npos && str_end != 0;
       str_end = path.find_last_of(':', str_end - 1)) {
    const string path_without_suffix = path.substr(0, str_end);
    assert(!path_without_suffix.empty());
    if (stat(path_without_suffix.c_str(), &dummy) == -1) {
      continue;
    }

    for (size_t i = 0; i < positions->size(); i++) {
      while (str_end < path.size() && ':' == path[str_end]) {
        str_end++;
      }
      if (str_end == path.size()) { break; }
      size_t next_str_end = path.find(':', str_end);
      const string arg = path.substr(str_end, next_str_end);
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
  return "";
}

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;

bool SaveContentsToFile(
    EditorState* editor_state, OpenBuffer* buffer, const string& path) {
  assert(buffer != nullptr);
  string tmp_path = path + ".tmp";
  int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if (fd == -1) {
    editor_state->SetStatus(tmp_path + ": open failed: " + strerror(errno));
    return false;
  }
  bool result = SaveContentsToOpenFile(editor_state, buffer, tmp_path, fd);
  close(fd);
  if (!result) {
    return false;
  }

  if (rename(tmp_path.c_str(), path.c_str()) == -1) {
    editor_state->SetStatus(path + ": rename failed: " + strerror(errno));
    return false;
  }

  return true;
}

bool SaveContentsToOpenFile(
    EditorState* editor_state, OpenBuffer* buffer, const string& path,
    int fd) {
  // TODO: It'd be significant more efficient to do fewer (bigger) writes.
  for (auto it = buffer->contents()->begin(); it != buffer->contents()->end();
       ++it) {
    const auto& str = (*it)->contents();
    char* tmp = static_cast<char*>(malloc(str->size() + 1));
    strcpy(tmp, str->ToString().c_str());
    tmp[str->size()] = '\n';
    bool include_newline = it + 1 != buffer->contents()->end();
    int write_result = write(fd, tmp, str->size() + (include_newline ? 1 : 0));
    free(tmp);
    if (write_result == -1) {
      editor_state->SetStatus(
          path + ": write failed: " + to_string(fd) + ": " + strerror(errno));
      return false;
    }
  }
  return true;
}

map<string, shared_ptr<OpenBuffer>>::iterator OpenFile(
    const OpenFileOptions& options) {
  EditorState* editor_state = options.editor_state;
  vector<int> tokens { 0, 0 };
  string pattern;
  string expanded_path = editor_state->expand_path(options.path);
  string actual_path = FindPath(expanded_path, &tokens, &pattern);
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
  string name;
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
    EditorState* editor_state, const string& path,
    bool ignore_if_not_found) {
  return std::move(unique_ptr<EditorMode>(
      new FileLinkMode(path, ignore_if_not_found)));
}

}  // namespace afc
}  // namespace editor
