#ifndef __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__
#define __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/decaying_counter.h"
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

    // Ownership of the file descriptior (i.e, the responsibility for closing
    // it) is transferred to the FileDescriptorReader.
    int fd;

    LineModifierSet modifiers;

    BufferTerminal* terminal;
  };

  FileDescriptorReader(Options options);
  ~FileDescriptorReader();

  int fd() const;
  struct timespec last_input_received() const;
  double lines_read_rate() const;

  enum class ReadResult {
    // If this is returned, no further calls to ReadData should happen (and our
    // customer should probably drop this instance as soon as feasible).
    kDone,
    // If this is returned, we haven't finished reading. The customer should
    // continue to call ReadData once it detects that more data is available.
    kContinue
  };
  ReadResult ReadData();

 private:
  const Options options_;

  // We read directly into low_buffer_ and then drain from that into
  // options_.buffer. It's possible that not all bytes read can be converted
  // (for example, if the reading stops in the middle of a wide character).
  std::unique_ptr<char[]> low_buffer_;
  size_t low_buffer_length_ = 0;

  mutable struct timespec last_input_received_ = {0, 0};

  DecayingCounter lines_read_rate_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__
