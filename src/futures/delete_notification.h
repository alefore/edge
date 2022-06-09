#ifndef __AFC_FUTURES_DELETE_NOTIFICATION_H__
#define __AFC_FUTURES_DELETE_NOTIFICATION_H__

#include "src/futures/futures.h"
#include "src/futures/listenable_value.h"
#include "src/language/value_or_error.h"

namespace afc::futures {
class DeleteNotification {
 public:
  DeleteNotification();

  ~DeleteNotification();

  language::NonNull<std::shared_ptr<ListenableValue<language::EmptyValue>>>
  listenable_value() const;

 private:
  DeleteNotification(futures::Future<language::EmptyValue> future);

  const futures::Value<language::EmptyValue>::Consumer consumer_;
  const language::NonNull<
      std::shared_ptr<futures::ListenableValue<language::EmptyValue>>>
      listenable_value_;
};
}  // namespace afc::futures
#endif  // __AFC_FUTURES_DELETE_NOTIFICATION_H__
