// Tracks number of times an operation happens (globally), as well as total time
// spent executing it.
//
// Example:
//
//     TRACK_OPERATION(BufferMetadataOutput_Prepare_AddMetadataForMark);
//
// The operation will finish when the scope (in which this expression was added)
// is exited.
//
// If you to explicitly control when the operation finishes:
//
//     auto call = INLINE_TRACKER(MyTrackerName);
//     …  heavy processing here …
//     call = nullptr;  // The operation finished.

#ifndef __AFC_EDITOR_SRC_TRACKERS_H__
#define __AFC_EDITOR_SRC_TRACKERS_H__

#include <functional>
#include <list>
#include <memory>
#include <string>

#include "src/concurrent/protected.h"

namespace afc::infrastructure {
// When an operation starts, just call tracker. Capture the returned value and
// discard it when the operation completes:
//
//     if (something) {
//       static Tracker tracker(L"Line::Output");
//       auto call = tracker.Call();
//       ... heavy evaluation ...
//     }
//
// Prefer to use the TRACK_OPERATION or INLINE_TRACKER macros.
//
// This class is thread-safe.
class Tracker {
 public:
  struct Data {
    const std::wstring name;

    size_t executions = 0;
    double seconds = 0;
    double longest_seconds = 0;
  };

  static std::list<Data> GetData();

  Tracker(std::wstring name);
  // Deleting a Tracker crashes the program. We deliberately prefer to force our
  // customers to retain (leak) Tracker instances; otherwise, we may have issues
  // with potential reuse of already deleted objects during shutdown (yay, C++'s
  // static initialization order fiasco).
  ~Tracker();

  std::unique_ptr<bool, std::function<void(bool*)>> Call();

 private:
  const std::list<Tracker*>::iterator trackers_it_;

  concurrent::Protected<Data> data_;
};

#define LSTR(x) L##x

#define INLINE_TRACKER(tracker_name)                           \
  std::invoke([] {                                             \
    static afc::infrastructure::Tracker* internal_tracker =    \
        new afc::infrastructure::Tracker(LSTR(#tracker_name)); \
    return internal_tracker->Call();                           \
  })

#define TRACK_OPERATION(tracker_name) \
  auto tracker_name##_call = INLINE_TRACKER(tracker_name)

}  // namespace afc::infrastructure

#endif  // __AFC_EDITOR_SRC_TRACKERS_H__
