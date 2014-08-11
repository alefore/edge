extern "C" {
#include <sys/mman.h>
}

#include "buffer.h"
#include "memory_mapped_file.h"

namespace afc {
namespace editor {

static struct stat StatFD(int fd) {
  struct stat output;
  if (fstat(fd, &output) == -1) { exit(1); }
  return output;
}

static char* LoadFile(int fd, size_t size) {
  void* addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED)
    exit(1);
  return static_cast<char*>(addr);
}

MemoryMappedFile::MemoryMappedFile(const string& path)
    : path_(path),
      fd_(open(path_.c_str(), O_RDONLY)),
      stat_buffer_(StatFD(fd_)),
      buffer_(LoadFile(fd_, stat_buffer_.st_size)) {}

MemoryMappedFile::~MemoryMappedFile() {
  munmap(buffer_, stat_buffer_.st_size);
  close(fd_);
}

void LoadMemoryMappedFile(const string& path, OpenBuffer* buffer) {
  shared_ptr<MemoryMappedFile> file(new MemoryMappedFile(path));
  buffer->AppendLazyString(file);
}

}  // namespace editor
}  // namespace afc
