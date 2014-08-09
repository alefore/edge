#include <cassert>
#include <iostream>
#include <memory>
#include <string>


extern "C" {
#include <dirent.h>
}

#include "char_buffer.h"
#include "file_link_mode.h"
#include "editor.h"

namespace afc {
namespace editor {

using std::cerr;
using std::unique_ptr;
using std::shared_ptr;

class FileLinkMode : public EditorMode {
 public:
  FileLinkMode(const char* path, size_t position)
      : path_(path), position_(position) {}

  void ProcessInput(int c, EditorState* editor_state) {
    shared_ptr<OpenBuffer> buffer(Load().release());
    if (buffer.get() == nullptr) {
      return;
    }
    buffer->current_position_line = position_;
    editor_state->buffers.push_back(buffer);
    editor_state->current_buffer = editor_state->buffers.size() - 1;
  }

 private:
  unique_ptr<OpenBuffer> Load() {
    cerr << "LOADING: " << path_;
    struct stat sb;
    if (stat(path_.c_str(), &sb) == -1) {
      return nullptr;
    }

    if (S_ISDIR(sb.st_mode)) {
      unique_ptr<OpenBuffer> buffer(new OpenBuffer());
      DIR* dir = opendir(path_.c_str());
      assert(dir != nullptr);
      struct dirent* entry;
      while ((entry = readdir(dir)) != nullptr) {
        unique_ptr<Line> line(new Line);
        line->contents.reset(NewCopyCharBuffer(entry->d_name).release());
        line->activate.reset(NewFileLinkMode(entry->d_name, 0).release());
        buffer->contents.push_back(std::move(line));
      }
      closedir(dir);
      return std::move(buffer);
    }

    unique_ptr<MemoryMappedFile> file(new MemoryMappedFile(path_));
    return unique_ptr<OpenBuffer>(new OpenBuffer(std::move(file)));
  }

  string path_;
  size_t position_;
};

unique_ptr<EditorMode> NewFileLinkMode(const char* path, int position) {
  return std::move(unique_ptr<EditorMode>(new FileLinkMode(path, position)));
}

}  // namespace afc
}  // namespace editor
