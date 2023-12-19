// `DeleteNotification` is used to support cancellation.
//
// Suppose you have a consumer of an abstract value `LineSequence` (there is
// nothing specific about `LineSequence`, we're using it simply as an example),
// which is produced asynchronously. Assume that sometimes the consumer wants to
// explicitly signal that the value no longer needs to be produced (e.g.,
// because some underlying data has changed, so the `LineSequence` being
// produced is no longer relevant). The consumer may want to signal this
// explicitly in order to conserve resources.
//
// The consumer creates a `DeleteNotification` instance and retains it as long
// as it remains interested in the value being produced. When starting the
// asynchronous production of the `LineSequence`, the consumer calls
// `DeleteNotification::listenable_value` and passes the resulting
// `DeleteNotification::Value` to the producer. The producer holds the Value and
// can use it to detect that the consumer has lost interest in the
// `LineSequence` being produced (and thus the asynchronous computation should
// be aborted).
//
// For example:
//
// struct Cancelled {};
//
// template <typename Value>
// using CancelledOr<Value> = std::variant<Cancelled, Value>;
//
// class State {
//  public:
//   void ReceiveInput(std::string value) {
//     // Aborts any previous asynchronous processing and prepares to kick off
//     // a new call (to `ProcessNewValue`):
//     delete_notification = MakeNonNullUnique<DeleteNotification>();
//
//     // Kick off heavy processing and display results when they are ready.
//     ProcessNewvalue(value, delete_notification.listenable_value())
//         .Transform([](CancelledOr<Output> output) {
//           if (auto* output_value = std::get_if<Output>(&output);
//               output_value != nullptr) {
//             DisplayResults(output_value);
//           }
//           return futures::Past(EmptyResult());
//         });
//   }
//
//  private:
//   futures::Value<CancelledOr<Output>> ProcessNewValue(
//       std::string value,
//       futures::ListenableValue<EmptyValue> cancel_notification) {
//     // Asynchronous processing of `value`, to produce some `Output` value.
//     ProcessId child_pid = LaunchChildProcess(value);
//
//     // If `cancel_notification`, kill the process early:
//     cancel_notification.AddListener(
//         [child_pid](EmptyValue&) {
//           file_system_driver->Kill(child_pid, SIGTERM);
//         });
//
//     ...
//
//     return file_system_driver->WaitPid(child_pid, 0)
//         .Transform([...](WaitPidOutput output) { return ReadInput(...); });
//   }
//
//   NonNull<std::unique_ptr<DeleteNotification>> delete_notification;
// };

#ifndef __AFC_FUTURES_DELETE_NOTIFICATION_H__
#define __AFC_FUTURES_DELETE_NOTIFICATION_H__

#include "src/futures/futures.h"
#include "src/futures/listenable_value.h"
#include "src/language/error/value_or_error.h"

namespace afc::futures {
class DeleteNotification {
 public:
  using Value = ListenableValue<language::EmptyValue>;

  static Value Never();

  DeleteNotification();
  DeleteNotification(const DeleteNotification&) = delete;
  DeleteNotification(DeleteNotification&&) = delete;

  ~DeleteNotification();

  Value listenable_value() const;

 private:
  DeleteNotification(futures::Future<language::EmptyValue> future);

  futures::Value<language::EmptyValue>::Consumer consumer_;
  const ListenableValue<language::EmptyValue> listenable_value_;
};
}  // namespace afc::futures
#endif  // __AFC_FUTURES_DELETE_NOTIFICATION_H__
