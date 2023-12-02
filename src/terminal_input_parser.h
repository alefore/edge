#ifndef __AFC_EDITOR_BUFFER_TERMINAL_H__
#define __AFC_EDITOR_BUFFER_TERMINAL_H__

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/concurrent/thread_pool.h"
#include "src/futures/futures.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"
#include "src/tests/fuzz_testable.h"

namespace afc::infrastructure::audio {
class Player;
}
namespace afc::editor {
class BufferName;

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

// Decodes input from a terminal-associated file descriptor.
//
// This input is received incrementally through `ReceiveInput`. As it is
// decoded, `TerminalAdapter` calls the associated methods in the
// `Receiver` instance.
class TerminalAdapter : public tests::fuzz::FuzzTestable, public FileAdapter {
 public:
  // Receiver contains methods that propagates commands that Edge receives from
  // the Tty to the OpenBuffer class. For example, the tty may send a code that
  // says "clear the screen"; this is turned into a call to
  // `Receiver::EraseLines`.
  class Receiver {
   public:
    virtual ~Receiver() = default;

    // Erases all lines in range [first, last).
    virtual void EraseLines(language::text::LineNumber first,
                            language::text::LineNumber last) = 0;

    virtual void AppendEmptyLine() = 0;

    virtual BufferName name() = 0;

    // The underlying file descriptor.
    virtual std::optional<infrastructure::FileDescriptor> fd() = 0;

    // Every buffer should keep track of the last size of a widget that has
    // displayed it. TerminalAdapter uses this to be notified when it
    // changes and propagate that information to the underlying file descriptor
    // (e.g., so that $LINES shell variable is updated).
    virtual language::ObservableValue<language::text::LineColumnDelta>&
    view_size() = 0;

    virtual void Bell() = 0;
    virtual void Warn(language::Error error) = 0;

    virtual const language::text::LineSequence contents() = 0;

    // Return the position of the start of the current view.
    virtual language::text::LineColumn current_widget_view_start() = 0;

    virtual void JumpToPosition(language::text::LineColumn position) = 0;
  };

 private:
  struct Data {
    // The last size written to buffer->fd() by UpdateSize.
    std::optional<language::text::LineColumnDelta> last_updated_size =
        std::nullopt;

    language::NonNull<std::unique_ptr<Receiver>> receiver;

    // TODO: Find a way to remove this? I.e. always use buffer_.
    language::text::MutableLineSequence& contents;

    language::text::LineColumn position = language::text::LineColumn();
  };

  const language::NonNull<std::shared_ptr<Data>> data_;

 public:
  TerminalAdapter(language::NonNull<std::unique_ptr<Receiver>> receiver,
                  language::text::MutableLineSequence& contents);

  void UpdateSize() override;

  std::optional<language::text::LineColumn> position() const override;
  void SetPositionToZero() override;

  futures::Value<language::EmptyValue> ReceiveInput(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>> str,
      const infrastructure::screen::LineModifierSet& modifiers,
      const std::function<void(language::text::LineNumberDelta)>&
          new_line_callback) override;

  bool WriteSignal(infrastructure::UnixSignal signal) override;

  std::vector<tests::fuzz::Handler> FuzzHandlers() override;

 private:
  static void InternalUpdateSize(Data& data);

  language::lazy_string::ColumnNumber ProcessTerminalEscapeSequence(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>> str,
      language::lazy_string::ColumnNumber read_index,
      infrastructure::screen::LineModifierSet* modifiers);

  void MoveToNextLine();

  static language::text::LineColumnDelta LastViewSize(Data& data);
};

class RegularFileAdapter : public FileAdapter {
 public:
  struct Options {
    concurrent::ThreadPoolWithWorkQueue& thread_pool;
    std::function<void(std::vector<language::NonNull<
                           std::shared_ptr<const language::text::Line>>>)>
        insert_lines;
  };

 private:
  const Options options_;

 public:
  RegularFileAdapter(Options options);
  void UpdateSize() override;

  std::optional<language::text::LineColumn> position() const override;
  void SetPositionToZero() override;

  futures::Value<language::EmptyValue> ReceiveInput(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>> str,
      const infrastructure::screen::LineModifierSet& modifiers,
      const std::function<void(language::text::LineNumberDelta)>&
          new_line_callback) override;

  bool WriteSignal(infrastructure::UnixSignal signal) override;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_TERMINAL_H__
