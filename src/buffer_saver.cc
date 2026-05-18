#include "src/buffer_saver.h"

using namespace afc::language;
using namespace afc::infrastructure;

namespace afc::editor {

/* static */ NonNull<std::shared_ptr<BufferSaver>> BufferSaver::New(
    Options options) {
  return MakeNonNullShared<BufferSaver>(ConstructorKey{}, std::move(options));
}

BufferSaver::BufferSaver(ConstructorKey, Options options)
    : options_(std::move(options)) {}

futures::Value<PossibleError> BufferSaver::Flush() const {
  VLOG(2) << "Flush!";
  return data_.lock([&](Data& data) -> futures::PossibleError {
    if (data.last_saved_contents == options_.contents_callback())
      return EmptyValue{};
    return FlushWithLock(data);
  });
}

void BufferSaver::QueueChange() const {
  VLOG(2) << "Queue change!";
  Time now = Now();
  bool change_detected = data_.lock([&](Data& data) {
    if (data.last_saved_contents == options_.contents_callback()) return false;
    if (!data.first_pending_change) data.first_pending_change = now;
    // Why use std::max? To deal with backward-jumping clocks.
    data.last_pending_change =
        std::max(now, data.last_pending_change.value_or(now));
    return true;
  });
  if (!change_detected) return;
  options_.work_queue->Wait(AddSeconds(now, options_.maximum_inactive_duration))
      .Transform([shared_this = shared_from_this()](EmptyValue) {
        VLOG(6) << "Inactive check!";
        shared_this->MaybeSave();
        return EmptyValue();
      });
  options_.work_queue->Wait(AddSeconds(now, options_.maximum_duration))
      .Transform([shared_this = shared_from_this()](EmptyValue) {
        VLOG(6) << "Maximum duration check!";
        shared_this->MaybeSave();
        return EmptyValue();
      });
}

futures::Value<PossibleError> BufferSaver::FlushWithLock(Data& data) const {
  CHECK_EQ(data.pending_save_consumer.has_value(),
           data.pending_save_future.has_value());

  if (data.save_ongoing) {
    if (!data.pending_save_future) {
      futures::Future<PossibleError> save_future;
      data.pending_save_future =
          futures::ListenableValue(std::move(save_future.value));
      data.pending_save_consumer = std::move(save_future.consumer);
    }
    return data.pending_save_future->ToFuture();
  }
  CHECK(!data.pending_save_future);
  CHECK(!data.pending_save_consumer);
  data.save_ongoing = true;
  futures::Future<PossibleError> output_future;
  // Why do we schedule it in the work queue (rather than run it directly)?
  // To avoid running the user's callback (and SaveFinished) under the lock.
  options_.work_queue->Schedule(
      {.callback = [shared_this = shared_from_this(),
                    consumer = std::move(output_future.consumer)] mutable {
        shared_this->options_.callback().SetConsumer(
            [shared_this,
             consumer = std::move(consumer)](PossibleError output) mutable {
              shared_this->SaveFinished();
              std::move(consumer)(std::move(output));
            });
      }});
  return std::move(output_future.value);
}

void BufferSaver::SaveFinished() const {
  return data_.lock([&](Data& data) {
    CHECK_EQ(data.pending_save_consumer.has_value(),
             data.pending_save_future.has_value());
    CHECK(data.save_ongoing);
    data.save_ongoing = false;
    if (!data.pending_save_future) return;
    auto pending_save_consumer = std::move(data.pending_save_consumer).value();
    data.pending_save_future = std::nullopt;
    data.pending_save_consumer = std::nullopt;
    FlushWithLock(data).SetConsumer(std::move(pending_save_consumer));
  });
}

void BufferSaver::MaybeSave() const {
  if (data_.lock([&](Data& data) {
        VLOG(4) << "Maybe save...";
        Time now = Now();
        CHECK_EQ(data.first_pending_change.has_value(),
                 data.last_pending_change.has_value());
        if (!data.first_pending_change) {
          VLOG(4) << "No changes.";
          return false;
        }
        CHECK(data.first_pending_change.value() <=
              data.last_pending_change.value());
        return SecondsBetween(data.first_pending_change.value(), now) >=
                   options_.maximum_duration ||
               SecondsBetween(data.last_pending_change.value(), now) >=
                   options_.maximum_inactive_duration;
      }))
    Flush();
}

}  // namespace afc::editor
