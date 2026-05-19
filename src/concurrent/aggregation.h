#pragma once

#include <glog/logging.h>

#include <algorithm>
#include <compare>
#include <optional>
#include <variant>
#include <vector>

#include "src/infrastructure/time.h"

namespace afc::concurrent::aggregation {
struct Timeouts {
  // Triggers a flush if the last pending input reaches this age.
  infrastructure::Duration inactive;
  // Triggers a flush when the first pending input reaches this age.
  infrastructure::Duration total;

  // For leading-edge debouncing. If no event has been registered in this
  // duration, the first event should be flushed right away (without any
  // aggregation).
  infrastructure::Duration post_flush;

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
  std::optional<infrastructure::Time> last_flush = std::nullopt;
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

    if (!last_flush ||
        first_pending_change.value() >=
            infrastructure::AddSeconds(last_flush.value(), timeouts.post_flush))
      return ActionFlush{};

    infrastructure::Time next_flush =
        std::min(infrastructure::AddSeconds(first_pending_change.value(),
                                            timeouts.total),
                 infrastructure::AddSeconds(last_pending_change.value(),
                                            timeouts.inactive));
    if (next_flush <= now) return ActionFlush{};
    return ActionWait{.next_check = next_flush};
  }

  std::vector<Event> StartFlush(infrastructure::Time now) {
    std::vector<Event> output;
    events.swap(output);
    first_pending_change = std::nullopt;
    last_pending_change = std::nullopt;
    last_flush = now;
    return output;
  }
};
}  // namespace afc::concurrent::aggregation
