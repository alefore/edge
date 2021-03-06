#include "src/futures/serializer.h"

#include "glog/logging.h"
#include "src/futures/futures.h"
#include "src/value_or_error.h"

namespace afc::futures {
void Serializer::Push(Callback callback) {
  futures::Future<afc::editor::EmptyValue> new_future;
  auto last_execution = std::move(last_execution_);
  last_execution_ = std::move(new_future.value);
  last_execution.SetConsumer(
      [callback = std::move(callback),
       consumer = std::move(new_future.consumer)](afc::editor::EmptyValue) {
        callback().SetConsumer(consumer);
      });
}
}  // namespace afc::futures
