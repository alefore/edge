#include "src/futures/delete_notification.h"

#include "src/language/safe_types.h"

namespace afc::futures {
using language::EmptyValue;
using language::MakeNonNullShared;
using language::NonNull;

DeleteNotification::DeleteNotification()
    : DeleteNotification(futures::Future<EmptyValue>()) {}

DeleteNotification::~DeleteNotification() { consumer_(EmptyValue()); }

NonNull<std::shared_ptr<ListenableValue<EmptyValue>>>
DeleteNotification::listenable_value() const {
  return listenable_value_;
}

DeleteNotification::DeleteNotification(futures::Future<EmptyValue> future)
    : consumer_(std::move(future.consumer)),
      listenable_value_(MakeNonNullShared<ListenableValue<EmptyValue>>(
          std::move(future.value))) {}
}  // namespace afc::futures
