#ifndef __AFC_EDITOR_MEMORY_MAPPED_FILE_H__
#define __AFC_EDITOR_MEMORY_MAPPED_FILE_H__

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
}

#include <string>

#include "lazy_string.h"

namespace afc {
namespace editor {

using std::string;

class MemoryMappedFile : public LazyString {
 public:
  MemoryMappedFile(const string& path);
  ~MemoryMappedFile();

  char get(size_t pos) const { return buffer_[pos]; }
  size_t size() const { return stat_buffer_.st_size; }

 private:
  const string path_;
  int fd_;
  struct stat stat_buffer_;
  char* buffer_;
};

}  // namespace editor
}  // namespace afc

#endif
