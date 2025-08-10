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
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/once_only_function.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"

namespace afc {
namespace editor {
// Description of the file descriptor, used for logging/debugging.
class FileDescriptorName
    : public language::GhostType<FileDescriptorName,
                                 language::lazy_string::LazyString> {
  using GhostType::GhostType;
};

// Class used to read input from a file descriptor into a buffer.
class FileDescriptorReader {
 public:
  struct Options {
    FileDescriptorName name;

    // Ownership of the file descriptior (i.e, the responsibility for closing
    // it) is transferred to the FileDescriptorReader.
    infrastructure::FileDescriptor fd;

    mutable language::OnceOnlyFunction<void()> receive_end_of_file;

    // When data is received, the FileDescriptorReader should call the
    // `receive_data` function. This function should return a future that gets
    // notified when the data has been successfully consumed.
    std::function<futures::Value<language::EmptyValue>(
        language::lazy_string::LazyString)>
        receive_data;
  };

  explicit FileDescriptorReader(Options options);
  ~FileDescriptorReader();

  infrastructure::FileDescriptor fd() const;
  struct timespec last_input_received() const;

  // Must not be called after `receive_end_of_file` has been called.
  void Register(infrastructure::execution::IterationHandler&);

 private:
  futures::Value<bool> ParseAndInsertLines(
      afc::language::lazy_string::LazyString contents);

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
