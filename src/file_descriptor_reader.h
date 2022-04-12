#ifndef __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__
#define __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__

#include <poll.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/async_processor.h"
#include "src/decaying_counter.h"
#include "src/ghost_type.h"
#include "src/lazy_string.h"
#include "src/line_column.h"
#include "src/line_modifier.h"

namespace afc {
namespace editor {

class OpenBuffer;
class BufferTerminal;

GHOST_TYPE(FileDescriptor, int);

// Class used to read input from a file descriptor into a buffer.
class FileDescriptorReader {
 public:
  struct Options {
    OpenBuffer& buffer;

    // Ownership of the file descriptior (i.e, the responsibility for closing
    // it) is transferred to the FileDescriptorReader.
    FileDescriptor fd;

    LineModifierSet modifiers;

    BufferTerminal* terminal = nullptr;

    // We want to avoid potentially expensive/slow parsing operations in the
    // main thread. To achieve that, we receive an async_processor owned by the
    // buffer and we delegate as much work as feasible to that processor.
    AsyncEvaluator& read_evaluator;
  };

  FileDescriptorReader(Options options);
  ~FileDescriptorReader();

  FileDescriptor fd() const;
  struct timespec last_input_received() const;
  double lines_read_rate() const;

  // Return a pollfd value that can be passed to `poll`. If the file isn't ready
  // for reading (e.g., a background operation is running on the data read),
  // returns std::nullopt.
  std::optional<struct pollfd> GetPollFd() const;

  enum class ReadResult {
    // If this is returned, no further calls to ReadData should happen (and our
    // customer should probably drop this instance as soon as feasible).
    kDone,
    // If this is returned, we haven't finished reading. The customer should
    // continue to call ReadData once it detects that more data is available.
    kContinue
  };
  futures::Value<ReadResult> ReadData();

 private:
  futures::Value<bool> ParseAndInsertLines(
      std::shared_ptr<LazyString> contents);

  const std::shared_ptr<const Options> options_;

  enum State { kIdle, kParsing };
  State state_ = State::kIdle;

  // We read directly into low_buffer_ and then drain from that into
  // options_.buffer. It's possible that not all bytes read can be converted
  // (for example, if the reading stops in the middle of a wide character).
  std::unique_ptr<char[]> low_buffer_;
  size_t low_buffer_length_ = 0;

  mutable struct timespec last_input_received_ = {0, 0};

  const std::shared_ptr<DecayingCounter> lines_read_rate_ =
      std::make_shared<DecayingCounter>(2.0);
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__
