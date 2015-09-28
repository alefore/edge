#include <cstring>
#include <iostream>

extern "C" {
#include <sys/mman.h>
#include <unistd.h>
}

#include "buffer.h"
#include "memory_mapped_file.h"
#include "wstring.h"

namespace afc {
namespace editor {

using std::cerr;
using std::endl;

static struct stat StatFD(int fd) {
  struct stat output;
  if (fstat(fd, &output) == -1) {
    cerr << "fstat failed.";
    exit(1);
  }
  return output;
}

static char* LoadFile(const string& path, int fd, size_t size) {
  if (size == 0) { return nullptr; }
  void* addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED) {
    cerr << path << ": mmap failed: " << strerror(errno) << endl;
    exit(1);
  }
  return static_cast<char*>(addr);
}

MemoryMappedFile::MemoryMappedFile(const wstring& path)
    : path_(ToByteString(path)),
      fd_(open(path_.c_str(), O_RDONLY)),
      stat_buffer_(StatFD(fd_)),
      buffer_(LoadFile(path_, fd_, stat_buffer_.st_size)) {
  LOG(INFO) << "Memory mapped file: " << path_;
  CHECK(size() == 0 || buffer_ != nullptr);
}

MemoryMappedFile::~MemoryMappedFile() {
  if (buffer_ != nullptr) { munmap(buffer_, stat_buffer_.st_size); }
  close(fd_);
}

void LoadMemoryMappedFile(
    EditorState* editor_state, const wstring& path, OpenBuffer* buffer) {
  shared_ptr<MemoryMappedFile> file(new MemoryMappedFile(path));
  buffer->AppendLazyString(editor_state, file);
}

}  // namespace editor
}  // namespace afc
