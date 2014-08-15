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
}

#include "char_buffer.h"
#include "file_link_mode.h"
#include "editor.h"
#include "run_command_handler.h"
#include "search_handler.h"

namespace {
using std::make_pair;
using std::shared_ptr;
using std::unique_ptr;
using std::sort;
using namespace afc::editor;

class FileBuffer : public OpenBuffer {
 public:
  FileBuffer(const string& path) : path_(path) {}

  void Reload(EditorState* editor_state) {
    struct stat sb;
    if (stat(path_.c_str(), &sb) == -1) {
      return;
    }

    contents_.clear();
    editor_state->CheckPosition();
    editor_state->screen_needs_redraw = true;

    if (!S_ISDIR(sb.st_mode)) {
      char* tmp = strdup(path_.c_str());
      if (0 == strcmp(basename(tmp), "passwd")) {
        RunCommandHandler("parsers/passwd <" + path_, editor_state);
      } else {
        LoadMemoryMappedFile(path_, this);
      }
      saveable_ = true;
      return;
    }

    unique_ptr<Line> line(new Line);
    line->contents.reset(NewCopyString("File listing: " + path_).release());
    contents_.push_back(std::move(line));

    DIR* dir = opendir(path_.c_str());
    assert(dir != nullptr);
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      unique_ptr<Line> line(new Line);
      line->contents.reset(NewCopyCharBuffer(entry->d_name).release());
      line->activate.reset(
          NewFileLinkMode(path_ + "/" + entry->d_name, 0, false).release());
      contents_.push_back(std::move(line));
    }
    closedir(dir);

    struct Compare {
      bool operator()(const shared_ptr<Line>& a, const shared_ptr<Line>& b) {
        return *a->contents < *b->contents;
      }
    } compare;

    sort(contents_.begin() + 1, contents_.end(), compare);
  }

 private:
  const string path_;
};

static char* realpath_safe(const string& path) {
  char* result = realpath(path.c_str(), nullptr);
  return result == nullptr ? strdup(path.c_str()) : result;
}

class FileLinkMode : public EditorMode {
 public:
  FileLinkMode(const string& path, size_t line, size_t col,
               const string& pattern)
      : path_(realpath_safe(path.c_str())),
        line_(line),
        col_(col),
        pattern_(pattern) {
    assert(path_.get() != nullptr);
  }

  void ProcessInput(int c, EditorState* editor_state) {
    editor_state->PushCurrentPosition();
    auto it = editor_state->buffers.insert(make_pair(path_.get(), nullptr));
    editor_state->current_buffer = it.first;
    if (it.second) {
      it.first->second.reset(new FileBuffer(path_.get()));
      it.first->second->Reload(editor_state);
    }
    it.first->second->set_current_position_line(line_);
    it.first->second->set_current_position_col(col_);
    it.first->second->CheckPosition();
    it.first->second->MaybeAdjustPositionCol();
    SearchHandler(pattern_, editor_state);
  }

  unique_ptr<char> path_;
  size_t line_;
  size_t col_;
  const string pattern_;
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

unique_ptr<EditorMode> NewFileLinkMode(
    const string& path, int position, bool ignore_if_not_found) {
  vector<int> tokens { position, 0 };
  string pattern;
  string actual_path = FindPath(path, &tokens, &pattern);
  if (actual_path.empty()) {
    if (ignore_if_not_found) {
      return nullptr;
    } else {
      actual_path = path;
    }
  }
  return std::move(unique_ptr<EditorMode>(
      new FileLinkMode(actual_path, tokens[0], tokens[1], pattern)));
}

}  // namespace afc
}  // namespace editor
