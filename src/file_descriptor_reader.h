#ifndef __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__
#define __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__

#include <poll.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/concurrent/thread_pool.h"
#include "src/futures/futures.h"
#include "src/infrastructure/execution.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/math/decaying_counter.h"

namespace afc {
namespace editor {
// Description of the file descriptor, used for logging/debugging.
//
// TODO(2023-12-02): This should use LazyString.
GHOST_TYPE(FileDescriptorName, std::wstring);

// Class used to read input from a file descriptor into a buffer.
class FileDescriptorReader {
 public:
  struct Options {
    FileDescriptorName name;

    // Ownership of the file descriptior (i.e, the responsibility for closing
    // it) is transferred to the FileDescriptorReader.
    infrastructure::FileDescriptor fd;

    std::function<void()> receive_end_of_file;
    std::function<void(
        language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>,
        std::function<void()>)>
        receive_data;
  };

  explicit FileDescriptorReader(Options options);
  ~FileDescriptorReader();

  infrastructure::FileDescriptor fd() const;
  struct timespec last_input_received() const;

  void Register(infrastructure::execution::IterationHandler&);

 private:
  futures::Value<bool> ParseAndInsertLines(
      language::NonNull<std::shared_ptr<afc::language::lazy_string::LazyString>>
          contents);

  const language::NonNull<std::shared_ptr<const Options>> options_;

  enum State { kReading, kProcessing };
  State state_ = State::kReading;

  // We read directly into low_buffer_ and then drain from that into
  // options_.buffer. It's possible that not all bytes read can be converted
  // (for example, if the reading stops in the middle of a wide character).
  std::unique_ptr<char[]> low_buffer_;
  size_t low_buffer_length_ = 0;

  mutable struct timespec last_input_received_ = {0, 0};
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FILE_DESCRIPTOR_READER_H__
