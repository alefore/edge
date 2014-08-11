#include <cassert>
#include <memory>
#include <string>
#include <algorithm>

extern "C" {
#include <dirent.h>
}

#include "char_buffer.h"
#include "file_link_mode.h"
#include "editor.h"

namespace {
using std::shared_ptr;
using std::sort;
using namespace afc::editor;

class FileLinkMode : public EditorMode {
 public:
  FileLinkMode(const string& path, size_t position)
      : path_(path), position_(position) {}

  void ProcessInput(int c, EditorState* editor_state) {
    auto it = editor_state->buffers.insert(make_pair(path_, nullptr));
    editor_state->current_buffer = it.first;
    if (it.second) {
      it.first->second.reset(Load().release());
    }
    it.first->second->current_position_line = position_;
    editor_state->screen_needs_redraw = true;
    editor_state->mode = std::move(NewCommandMode());
  }

 private:
  unique_ptr<OpenBuffer> Load() {
    unique_ptr<OpenBuffer> buffer(new OpenBuffer());
    struct stat sb;
    if (stat(path_.c_str(), &sb) == -1) {
      return std::move(buffer);
    }

    if (S_ISDIR(sb.st_mode)) {
      unique_ptr<Line> line(new Line);
      line->contents.reset(NewCopyString("File listing: " + path_).release());
      buffer->contents.push_back(std::move(line));

      DIR* dir = opendir(path_.c_str());
      assert(dir != nullptr);
      struct dirent* entry;
      string prefix = path_ == "." ? "" : path_ + "/";
      while ((entry = readdir(dir)) != nullptr) {
        unique_ptr<Line> line(new Line);
        line->contents.reset(NewCopyCharBuffer(entry->d_name).release());
        line->activate.reset(NewFileLinkMode(prefix + entry->d_name, 0).release());
        buffer->contents.push_back(std::move(line));
      }
      closedir(dir);

      struct Compare {
        bool operator()(const shared_ptr<Line>& a, const shared_ptr<Line>& b) {
          return *a->contents < *b->contents;
        }
      } compare;

      sort(buffer->contents.begin() + 1, buffer->contents.end(), compare);
      return std::move(buffer);
    }

    buffer.reset(new OpenBuffer(unique_ptr<MemoryMappedFile>(new MemoryMappedFile(path_))));
    buffer->saveable = true;
    return buffer;
  }

  string path_;
  size_t position_;
};

}  // namespace

namespace afc {
namespace editor {

using std::unique_ptr;
unique_ptr<EditorMode> NewFileLinkMode(const string& path, int position) {
  return std::move(unique_ptr<EditorMode>(new FileLinkMode(path, position)));
}

}  // namespace afc
}  // namespace editor
