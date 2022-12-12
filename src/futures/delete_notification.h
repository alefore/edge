#ifndef __AFC_FUTURES_DELETE_NOTIFICATION_H__
#define __AFC_FUTURES_DELETE_NOTIFICATION_H__

#include "src/futures/futures.h"
#include "src/futures/listenable_value.h"
#include "src/language/value_or_error.h"

namespace afc::futures {
// Useful to support cancellation. A consumer of an abstract value creates an
// instance and kicks off producers, passing them a `Value` as returned by
// `listenable_value`. The value will be notified when the `DeleteNotification`
// instance is deleted. So the consumer just needs to keep the
// `DeleteNotification` object alive for as long as it is interested in the
// original value.
class DeleteNotification {
 public:
  using Value = ListenableValue<language::EmptyValue>;

  static Value Never();

  DeleteNotification();

  ~DeleteNotification();

  Value listenable_value() const;

 private:
  DeleteNotification(futures::Future<language::EmptyValue> future);

  const futures::Value<language::EmptyValue>::Consumer consumer_;
  const ListenableValue<language::EmptyValue> listenable_value_;
};
}  // namespace afc::futures
#endif  // __AFC_FUTURES_DELETE_NOTIFICATION_H__
