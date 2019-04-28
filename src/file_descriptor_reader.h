#ifndef __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__
#define __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/lazy_string.h"
#include "src/line_column.h"
#include "src/line_modifier.h"

namespace afc {
namespace editor {

class OpenBuffer;
class BufferTerminal;

// Class used to read input from a file descriptor into a buffer.
class FileDescriptorReader {
 public:
  struct Options {
    OpenBuffer* buffer = nullptr;

    // Signals that we're done adding to the current line and a new line should
    // start.
    //
    // We do this rather than call the public methods of OpenBuffer that
    // have roughly the same effect to allow the buffer to scan the line (which
    // it won't do with the public methods).
    std::function<void()> start_new_line;
  };

  FileDescriptorReader(Options options);

  int fd() const;

  void Open(int fd, LineModifierSet modifiers, BufferTerminal* terminal);
  void Close();
  void Reset();
  void ReadData();

 private:
  const Options options_;

  // -1 means "no file descriptor" (i.e. not currently loading this).
  int fd_ = -1;

  // We read directly into low_buffer_ and then drain from that into buffer_.
  // It's possible that not all bytes read can be converted (for example, if the
  // reading stops in the middle of a wide character).
  std::unique_ptr<char[]> low_buffer_;
  size_t low_buffer_length_ = 0;

  LineModifierSet modifiers_;

  BufferTerminal* terminal_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__
