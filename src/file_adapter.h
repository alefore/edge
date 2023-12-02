#ifndef __AFC_EDITOR_FILE_ADAPTER_H__
#define __AFC_EDITOR_FILE_ADAPTER_H__

#include <functional>
#include <memory>
#include <optional>

#include "src/futures/futures.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"

namespace afc::editor {
// Class that represents the bridge between the OpenBuffer class and a file
// descriptor from which input is being received, and to which input can be
// propagated. Two subclasses are expected: one for file descriptors with a tty,
// and one for file descriptors without.
//
// Communication happens in both directions:
//
// - We process input received from the file descriptor (and update the contents
//   of the buffer).
//
// - When the OpenBuffer receives signals, we propagate them to the file
//   descriptor.
class FileAdapter {
 public:
  virtual ~FileAdapter() = default;

  // Propagates the last view size to buffer->fd().
  virtual void UpdateSize() = 0;

  virtual std::optional<language::text::LineColumn> position() const = 0;
  virtual void SetPositionToZero() = 0;
  virtual futures::Value<language::EmptyValue> ReceiveInput(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>> str,
      const infrastructure::screen::LineModifierSet& modifiers,
      const std::function<void(language::text::LineNumberDelta)>&
          new_line_callback) = 0;

  virtual bool WriteSignal(infrastructure::UnixSignal signal) = 0;
};
}  // namespace afc::editor
#endif  // __AFC_EDITOR_FILE_ADAPTER_H__
