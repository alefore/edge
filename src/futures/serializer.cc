#include "src/futures/serializer.h"

#include "glog/logging.h"
#include "src/futures/futures.h"
#include "src/language/errors/value_or_error.h"

namespace afc::futures {
void Serializer::Push(Callback callback) {
  futures::Future<language::EmptyValue> new_future;
  auto last_execution = std::move(last_execution_);
  last_execution_ = std::move(new_future.value);
  std::move(last_execution)
      .SetConsumer(
          [callback = std::move(callback),
           consumer = std::move(new_future.consumer)](language::EmptyValue) {
            callback().SetConsumer(consumer);
          });
}
}  // namespace afc::futures
