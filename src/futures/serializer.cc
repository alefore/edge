#include "src/futures/serializer.h"

#include "glog/logging.h"
#include "src/futures/futures.h"
#include "src/language/error/value_or_error.h"

namespace afc::futures {
void Serializer::Push(Callback callback) {
  auto last_execution = std::move(last_execution_);
  last_execution_ =
      std::move(last_execution)
          .Transform([callback = std::move(callback)](language::EmptyValue) {
            return callback();
          });
}
}  // namespace afc::futures
