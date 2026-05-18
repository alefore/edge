#pragma once

#include <memory>
#include <optional>

#include "src/concurrent/protected.h"
#include "src/concurrent/work_queue.h"
#include "src/futures/listenable_value.h"
#include "src/infrastructure/time.h"
#include "src/language/access_key.h"
#include "src/language/error/value_or_error.h"
#include "src/language/safe_types.h"

namespace afc::editor {
// Class responsible for saving contents of a buffer.
class BufferSaver : public std::enable_shared_from_this<BufferSaver> {
 public:
  using SaveCallback = std::function<futures::Value<language::PossibleError>()>;
  struct Options {
    SaveCallback callback;
    language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue;

    // Triggers a save after if this duration passes after the last change.
    infrastructure::Duration maximum_inactive_duration;

    // Triggers a save if this duration passes after a change (even if more
    // changes are being registered).
    infrastructure::Duration maximum_duration;
  };

 private:
  Options options_;
  struct Data {
    std::optional<infrastructure::Time> first_pending_change;
    std::optional<infrastructure::Time> last_pending_change;
    std::optional<infrastructure::Time> last_save_start;

    bool save_ongoing = false;

    // If a new save request arrives while one is ongoing, we set these two
    // fields.
    std::optional<futures::ListenableValue<language::PossibleError>>
        pending_save_future = std::nullopt;
    std::optional<futures::Value<language::PossibleError>::Consumer>
        pending_save_consumer = std::nullopt;
  };
  mutable concurrent::Protected<Data> data_;

 public:
  using ConstructorKey = afc::language::AccessKey<BufferSaver>;

  BufferSaver(ConstructorKey, Options options);

  // Why force the use of a static factory? To ensure all instances created are
  // std::shared_ptr (since we inherit from std::enable_shared_from_this).
  static language::NonNull<std::shared_ptr<BufferSaver>> New(Options options);

  // Forces a save as soon as possible.
  futures::Value<language::PossibleError> Flush() const;
  void QueueChange() const;

 private:
  futures::Value<language::PossibleError> FlushWithLock(Data& data) const;
  void SaveFinished() const;
  void MaybeSave() const;
};
}  // namespace afc::editor
