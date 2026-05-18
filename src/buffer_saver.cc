#include "src/buffer_saver.h"

#include "src/language/overload.h"
#include "src/tests/factory.h"

using namespace afc::language;
using namespace afc::infrastructure;

namespace afc::editor {
using StringScheduler = aggregation::Scheduler<std::string>;

TEST_GROUP(Aggregation_Scheduler_PushEvent,
           [](StringScheduler scheduler, Time now, std::string event) {
             scheduler.PushEvent(now, std::move(event));
             return scheduler;
           })
    .Add(L"Empty", StringScheduler({1.0, 10.0}), Time{10, 0}, "Event1",
         StringScheduler({1.0, 10.0}, Time{10, 0}, Time{10, 0}, {"Event1"}))
    .Add(L"NonEmpty",
         StringScheduler({1.0, 10.0}, Time{10, 0}, Time{10, 0}, {"Event1"}),
         Time{12, 0}, "Event2",
         StringScheduler{
             {1.0, 10.0}, Time{10, 0}, Time{12, 0}, {"Event1", "Event2"}})
    .Add(L"ClockJumpsBeforeFirst",
         StringScheduler({1.0, 10.0}, Time{10, 0}, Time{10, 0}, {"Event1"}),
         Time{5, 0}, "Event2",
         StringScheduler{
             {1.0, 10.0}, Time{5, 0}, Time{10, 0}, {"Event1", "Event2"}});

TEST_GROUP(Aggregation_Scheduler_StartFlush,
           [](StringScheduler scheduler) {
             std::vector<std::string> flushed_events = scheduler.StartFlush();
             return std::make_pair(flushed_events, scheduler);
           })
    .Add(L"Empty", StringScheduler({1.0, 10.0}),
         std::make_pair(std::vector<std::string>{},
                        StringScheduler{{1.0, 10.0}}))
    .Add(L"NonEmpty",
         StringScheduler{{1.0, 10.0}, Time{10, 0}, Time{12, 0}, {"A", "B"}},
         std::make_pair(std::vector<std::string>{"A", "B"},
                        StringScheduler{{1.0, 10.0}}));

TEST_GROUP(Aggregation_Scheduler_Check,
           [](const StringScheduler& scheduler, Time now) {
             return scheduler.Check(now);
           })
    .Add(L"Empty", StringScheduler{{1.0, 10.0}}, Time{15, 0},
         aggregation::ActionNone{})
    .Add(L"BeforeTimeoutNextInactive",
         StringScheduler{{1.0, 10.0}, Time{10, 0}, Time{18, 0}, {"A"}},
         Time{18, 500000000},
         aggregation::ActionWait{.next_check = Time{19, 0}})
    .Add(L"BeforeTimeoutNextGlobal",
         StringScheduler{
             {1.0, 10.0}, Time{10, 100000000}, Time{19, 500000000}, {"A"}},
         Time{19, 600000000},
         aggregation::ActionWait{.next_check = Time{20, 100000000}})
    .Add(L"ExactInactiveTimeout",
         StringScheduler{{1.0, 10.0}, Time{10, 0}, Time{10, 0}, {"A"}},
         Time{11, 0}, aggregation::ActionFlush{})
    .Add(L"ExactGlobalTimeout",
         StringScheduler{{1.0, 10.0}, Time{10, 100000000}, Time{20, 0}, {"A"}},
         Time{20, 100000000}, aggregation::ActionFlush{})
    .Add(L"PastInactiveTimeout",
         StringScheduler{{1.0, 10.0}, Time{10, 0}, Time{10, 0}, {"A"}},
         Time{12, 0}, aggregation::ActionFlush{})
    .Add(L"PastGlobalTimeout",
         StringScheduler{{1.0, 10.0}, Time{10, 0}, Time{20, 0}, {"A"}},
         Time{20, 100000000}, aggregation::ActionFlush{});

/* static */ NonNull<std::shared_ptr<BufferSaver>> BufferSaver::New(
    Options options) {
  return MakeNonNullShared<BufferSaver>(ConstructorKey{}, std::move(options));
}

BufferSaver::BufferSaver(ConstructorKey, Options options)
    : options_(std::move(options)),
      data_(Data{.scheduler =
                     aggregation::Scheduler<EmptyValue>(options_.timeouts)}) {}

void BufferSaver::Flush() const {
  VLOG(2) << "Flush!";
  data_.lock([&](Data& data) {
    if (data.last_saved_contents != options_.contents_callback())
      FlushWithLock(data);
  });
}

void BufferSaver::RecordChange() const {
  VLOG(2) << "Queue change!";
  data_.lock([&](Data& data) {
    if (data.last_saved_contents != options_.contents_callback())
      data.scheduler.PushEvent(Now(), EmptyValue{});
  });
  CheckScheduler();
}

void BufferSaver::FlushWithLock(Data& data) const {
  if (data.save_ongoing) return;
  data.save_ongoing = true;
  data.last_saved_contents = options_.contents_callback();
  data.scheduler.StartFlush();
  // Why do we schedule it in the work queue (rather than run it directly)?
  // To avoid running the user's callback (and SaveFinished) under the lock.
  options_.work_queue->Schedule(
      {.callback = [shared_this = shared_from_this()] {
        shared_this->options_.callback().SetConsumer(
            [shared_this](PossibleError) { shared_this->SaveFinished(); });
      }});
}

void BufferSaver::SaveFinished() const {
  data_.lock([&](Data& data) {
    CHECK(data.save_ongoing);
    data.save_ongoing = false;
  });
  CheckScheduler();
}

void BufferSaver::CheckScheduler() const {
  data_.lock([&](Data& data) {
    std::visit(overload{[](aggregation::ActionNone) {},
                        [&](aggregation::ActionFlush) { FlushWithLock(data); },
                        [&](aggregation::ActionWait action) {
                          if (data.check_already_scheduled) return;
                          data.check_already_scheduled = true;
                          options_.work_queue->Wait(action.next_check)
                              .Transform([shared_this =
                                              shared_from_this()](EmptyValue) {
                                shared_this->data_.lock([](Data& data_nested) {
                                  CHECK(data_nested.check_already_scheduled);
                                  data_nested.check_already_scheduled = false;
                                });
                                shared_this->CheckScheduler();
                                return EmptyValue();
                              });
                        }},
               data.scheduler.Check(Now()));
  });
}
}  // namespace afc::editor
