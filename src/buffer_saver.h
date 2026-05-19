#pragma once

#include <memory>
#include <optional>
#include <variant>

#include "src/concurrent/protected.h"
#include "src/concurrent/work_queue.h"
#include "src/futures/listenable_value.h"
#include "src/infrastructure/time.h"
#include "src/language/access_key.h"
#include "src/language/error/value_or_error.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_sequence.h"

namespace afc::editor {
namespace aggregation {
// TODO(2026-05-19, P2, trivial): Move this to //src/concurrent.
struct Timeouts {
  // Triggers a flush if the last pending input reaches this age.
  infrastructure::Duration inactive;
  // Triggers a flush when the first pending input reaches this age.
  infrastructure::Duration total;

  std::strong_ordering operator<=>(const Timeouts&) const = default;
};

struct ActionFlush {
  std::strong_ordering operator<=>(const ActionFlush&) const = default;
};
struct ActionWait {
  infrastructure::Time next_check;
  std::strong_ordering operator<=>(const ActionWait&) const = default;
};
struct ActionNone {
  std::strong_ordering operator<=>(const ActionNone&) const = default;
};

using CheckResult = std::variant<ActionFlush, ActionWait, ActionNone>;

template <typename Event>
struct Scheduler {
  std::optional<infrastructure::Time> first_pending_change = std::nullopt;
  std::optional<infrastructure::Time> last_pending_change = std::nullopt;
  std::vector<Event> events;

  void PushEvent(infrastructure::Time now, Event event) {
    events.push_back(std::move(event));
    if (!first_pending_change || first_pending_change.value() > now)
      first_pending_change = now;
    last_pending_change = std::max(now, last_pending_change.value_or(now));
  }

  CheckResult Check(infrastructure::Time now, const Timeouts& timeouts) const {
    CHECK_EQ(first_pending_change.has_value(), !events.empty());
    CHECK_EQ(last_pending_change.has_value(), !events.empty());
    if (events.empty()) return ActionNone{};
    CHECK(first_pending_change.value() <= last_pending_change.value());
    infrastructure::Time next_flush =
        std::min(infrastructure::AddSeconds(first_pending_change.value(),
                                            timeouts.total),
                 infrastructure::AddSeconds(last_pending_change.value(),
                                            timeouts.inactive));
    if (next_flush <= now) return ActionFlush{};
    return ActionWait{.next_check = next_flush};
  }

  std::vector<Event> StartFlush() {
    std::vector<Event> output;
    events.swap(output);
    first_pending_change = std::nullopt;
    last_pending_change = std::nullopt;
    return output;
  }

  std::strong_ordering operator<=>(const Scheduler&) const = default;
};
}  // namespace aggregation

// Class responsible for saving contents of a buffer.
class BufferSaver : public std::enable_shared_from_this<BufferSaver> {
 public:
  using SaveCallback = std::function<futures::Value<language::PossibleError>()>;

  struct Options {
    SaveCallback callback;
    std::function<language::text::LineSequence(void)> contents_callback;
    language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue;
    aggregation::Timeouts timeouts;
  };

 private:
  Options options_;

  struct Data {
    aggregation::Scheduler<language::EmptyValue> scheduler;

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
  void FlushWithLock(Data& data) const;
  void SaveFinished() const;
  void CheckScheduler() const;
};
}  // namespace afc::editor
