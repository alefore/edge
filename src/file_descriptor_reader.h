#ifndef __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__
#define __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__

#include <poll.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/buffer_name.h"
#include "src/concurrent/thread_pool.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/math/decaying_counter.h"

namespace afc {
namespace editor {

class OpenBuffer;

// Class used to read input from a file descriptor into a buffer.
class FileDescriptorReader {
 public:
  struct Options {
    BufferName buffer_name;

    std::function<void(const language::lazy_string::LazyString&)> maybe_exec;
    std::function<void(std::vector<language::NonNull<
                           std::shared_ptr<const language::text::Line>>>)>
        insert_lines;

    // Should be null if there's no terminal.
    std::function<void(const language::NonNull<
                           std::shared_ptr<language::lazy_string::LazyString>>&,
                       std::function<void()>)>
        process_terminal_input;

    // Ownership of the file descriptior (i.e, the responsibility for closing
    // it) is transferred to the FileDescriptorReader.
    infrastructure::FileDescriptor fd;

    LineModifierSet modifiers;

    // We want to avoid potentially expensive/slow parsing operations in the
    // main thread. To achieve that, we receive a thread pool owned by our
    // customer and we delegate as much work as feasible to it.
    concurrent::ThreadPool& thread_pool;
  };

  FileDescriptorReader(Options options);
  ~FileDescriptorReader();

  infrastructure::FileDescriptor fd() const;
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
      language::NonNull<std::shared_ptr<afc::language::lazy_string::LazyString>>
          contents);

  const std::shared_ptr<const Options> options_;

  enum State { kIdle, kParsing };
  State state_ = State::kIdle;

  // We read directly into low_buffer_ and then drain from that into
  // options_.buffer. It's possible that not all bytes read can be converted
  // (for example, if the reading stops in the middle of a wide character).
  std::unique_ptr<char[]> low_buffer_;
  size_t low_buffer_length_ = 0;

  mutable struct timespec last_input_received_ = {0, 0};

  const language::NonNull<std::shared_ptr<math::DecayingCounter>>
      lines_read_rate_ =
          language::MakeNonNullShared<math::DecayingCounter>(2.0);
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__
