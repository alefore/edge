#ifndef __AFC_EDITOR_FUTURES_SERIALIZER_H__
#define __AFC_EDITOR_FUTURES_SERIALIZER_H__

#include <glog/logging.h>

#include <deque>
#include <functional>
#include <memory>
#include <type_traits>

#include "src/futures/futures.h"
#include "src/value_or_error.h"

namespace afc::futures {
// Receives multiple callbacks concurrently, each returning a future. Ensures
// that they are only executed serially.
//
// The serializer may be deleted before all callbacks have executed; they will
// still execute.
//
// This class is thread-safe. If the futures schedule asynchronous work, they
// must make sure that the notification happens in the same thread that calls
// Push.
class Serializer {
 public:
  using Callback = std::function<futures::Value<afc::editor::EmptyValue>()>;
  void Push(Callback callback);

 private:
  futures::Value<afc::editor::EmptyValue> last_execution_ =
      futures::Past(afc::editor::EmptyValue());
};
}  // namespace afc::futures

#endif  // __AFC_EDITOR_FUTURES_SERIALIZER_H__
