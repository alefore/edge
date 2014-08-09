#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>

extern "C" {
#include <sys/types.h>
#include <fcntl.h>
}

#include "editor.h"
#include "lazy_string.h"
#include "line_parser.h"
#include "memory_mapped_file.h"
#include "terminal.h"
#include "token.h"

namespace afc {
namespace editor {

using std::string;

class CharBuffer : public LazyString {
 public:
  CharBuffer(char* buffer, size_t size) : buffer_(buffer), size_(size) {}

  char get(size_t pos) { return buffer_[pos]; }
  size_t size() { return size_; }

 private:
  char* buffer_;
  int size_;
};

static std::unique_ptr<Token> ParseFromPath(const std::string& path) {
  std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
  std::stringstream sstr;
  sstr << input.rdbuf();
  return Parse(sstr.str());
}

}  // namespace editor
}  // namespace afc

int main(int argc, const char* argv[]) {
  using namespace afc::editor;
  using std::unique_ptr;

  Terminal terminal;
  terminal.SetStatus("Loading file: editor_cp.cc");
  unique_ptr<MemoryMappedFile> file(new MemoryMappedFile("editor_cp.cc"));
  EditorState editor_state;
  editor_state.buffers.push_back(shared_ptr<OpenBuffer>(new OpenBuffer(std::move(file))));

  while (!editor_state.terminate) {
    terminal.Display(&editor_state);
    int c = terminal.Read();
    editor_state.mode->ProcessInput(c, &editor_state);
  }
  terminal.SetStatus("done");
}
