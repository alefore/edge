#pragma once

#include <memory>
#include <optional>
#include <variant>

#include "src/concurrent/aggregation.h"
#include "src/concurrent/protected.h"
#include "src/concurrent/work_queue.h"
#include "src/futures/listenable_value.h"
#include "src/infrastructure/time.h"
#include "src/language/access_key.h"
#include "src/language/error/value_or_error.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_sequence.h"

namespace afc::editor {

// Class responsible for saving contents of a buffer.
class BufferSaver : public std::enable_shared_from_this<BufferSaver> {
 public:
  using SaveCallback = std::function<futures::Value<language::PossibleError>()>;

  struct Options {
    SaveCallback callback;
    std::function<language::text::LineSequence(void)> contents_callback;
    std::weak_ptr<concurrent::WorkQueue> work_queue;
    concurrent::aggregation::Timeouts timeouts;
  };

 private:
  Options options_;

  struct Data {
    concurrent::aggregation::Scheduler<language::EmptyValue> scheduler;

    std::optional<language::text::LineSequence> last_saved_contents =
        std::nullopt;

    bool save_ongoing = false;
    bool check_already_scheduled = false;
  };
  mutable concurrent::Protected<Data> data_;

 public:
  using ConstructorKey = afc::language::AccessKey<BufferSaver>;

  BufferSaver(ConstructorKey, Options options);

  // Why force the use of a static factory? To ensure all instances created are
  // std::shared_ptr (since we inherit from std::enable_shared_from_this).
  static language::NonNull<std::shared_ptr<BufferSaver>> New(Options options);

  // Forces a save as soon as possible.
  void Flush() const;

  void RecordChange() const;

 private:
  std::function<void()> FlushWithLock(Data& data) const;
  std::function<void()> CheckSchedulerWithLock(Data& data) const;
  void SaveFinished() const;
  void CheckScheduler() const;
};
}  // namespace afc::editor
