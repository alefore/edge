#include "src/buffer_saver.h"

#include "src/language/overload.h"
#include "src/tests/factory.h"

namespace aggregation = afc::concurrent::aggregation;

using namespace afc::language;
using namespace afc::infrastructure;

namespace afc::editor {
/* static */ NonNull<std::shared_ptr<BufferSaver>> BufferSaver::New(
    Options options) {
  return MakeNonNullShared<BufferSaver>(ConstructorKey{}, std::move(options));
}

BufferSaver::BufferSaver(ConstructorKey, Options options)
    : options_(std::move(options)) {}

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
               data.scheduler.Check(Now(), options_.timeouts));
  });
}
}  // namespace afc::editor
