#include "src/concurrent/aggregation.h"

#include "src/tests/factory.h"

using namespace afc::infrastructure;

namespace afc::concurrent::aggregation {
using StringScheduler = aggregation::Scheduler<std::string>;

bool operator==(const StringScheduler& lhs, const StringScheduler& rhs) {
  return lhs.first_pending_change == rhs.first_pending_change &&
         lhs.last_pending_change == rhs.last_pending_change &&
         lhs.events == rhs.events;
}

namespace {
TEST_GROUP(Aggregation_Scheduler_PushEvent,
           [](StringScheduler scheduler, Time now, std::string event) {
             scheduler.PushEvent(now, std::move(event));
             return scheduler;
           })
    .Add(L"Empty", StringScheduler{}, Time{10, 0}, "Event1",
         StringScheduler{Time{10, 0}, Time{10, 0}, std::nullopt, {"Event1"}})
    .Add(L"NonEmpty",
         StringScheduler{Time{10, 0}, Time{10, 0}, std::nullopt, {"Event1"}},
         Time{12, 0}, "Event2",
         StringScheduler{
             Time{10, 0}, Time{12, 0}, std::nullopt, {"Event1", "Event2"}})
    .Add(L"ClockJumpsBeforeFirst",
         StringScheduler{Time{10, 0}, Time{10, 0}, std::nullopt, {"Event1"}},
         Time{5, 0}, "Event2",
         StringScheduler{
             Time{5, 0}, Time{10, 0}, std::nullopt, {"Event1", "Event2"}});

TEST_GROUP(Aggregation_Scheduler_StartFlush,
           [](StringScheduler scheduler) {
             std::vector<std::string> flushed_events =
                 scheduler.StartFlush(Time{10, 0});
             return std::make_pair(flushed_events, scheduler);
           })
    .Add(L"Empty", StringScheduler(),
         std::make_pair(std::vector<std::string>{}, StringScheduler{}))
    .Add(L"NonEmpty",
         StringScheduler{Time{10, 0}, Time{12, 0}, Time{12, 0}, {"A", "B"}},
         std::make_pair(std::vector<std::string>{"A", "B"}, StringScheduler{}));

TEST_GROUP(Aggregation_Scheduler_Check,
           [](const StringScheduler& scheduler, Time now) {
             return scheduler.Check(now,
                                    aggregation::Timeouts{1.0, 10.0, 30.0});
           })
    .Add(L"Empty", StringScheduler{}, Time{15, 0}, aggregation::ActionNone{})
    .Add(L"BeforeTimeoutNextInactive",
         StringScheduler{Time{10, 0}, Time{18, 0}, Time{9, 0}, {"A"}},
         Time{18, 500000000},
         aggregation::ActionWait{.next_check = Time{19, 0}})
    .Add(L"BeforeTimeoutNextGlobal",
         StringScheduler{
             Time{10, 100000000}, Time{19, 500000000}, Time{9, 0}, {"A"}},
         Time{19, 600000000},
         aggregation::ActionWait{.next_check = Time{20, 100000000}})
    .Add(L"ExactInactiveTimeout",
         StringScheduler{Time{10, 0}, Time{10, 0}, Time{9, 0}, {"A"}},
         Time{11, 0}, aggregation::ActionFlush{})
    .Add(L"ExactGlobalTimeout",
         StringScheduler{Time{10, 100000000}, Time{20, 0}, Time{9, 0}, {"A"}},
         Time{20, 100000000}, aggregation::ActionFlush{})
    .Add(L"PastInactiveTimeout",
         StringScheduler{Time{10, 0}, Time{10, 0}, Time{9, 0}, {"A"}},
         Time{12, 0}, aggregation::ActionFlush{})
    .Add(L"PastGlobalTimeout",
         StringScheduler{Time{10, 0}, Time{20, 0}, Time{9, 0}, {"A"}},
         Time{20, 100000000}, aggregation::ActionFlush{})
    .Add(L"InstantFlush_NoPreviousFlush",
         StringScheduler{Time{10, 0}, Time{10, 0}, std::nullopt, {"A"}},
         Time{10, 0}, aggregation::ActionFlush{})
    .Add(L"InstantFlush_PastPostFlushCooldown",
         StringScheduler{Time{45, 0}, Time{45, 0}, Time{10, 0}, {"A"}},
         Time{45, 0}, aggregation::ActionFlush{});
}  // namespace
}  // namespace afc::concurrent::aggregation
