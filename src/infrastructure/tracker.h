#ifndef __AFC_EDITOR_SRC_TRACKERS_H__
#define __AFC_EDITOR_SRC_TRACKERS_H__

#include <functional>
#include <list>
#include <memory>
#include <string>

#include "src/concurrent/protected.h"

namespace afc::infrastructure {

// Tracks number of times an operation happens (globally), as well as total time
// spent executing it.
//
// When an operation starts, just call tracker. Capture the returned value and
// discard it when the operation completes:
//
//     if (something) {
//       static Tracker tracker(L"Line::Output");
//       auto call = tracker.Call();
//       ... heavy evaluation ...
//     }
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
  ~Tracker();

  std::unique_ptr<bool, std::function<void(bool*)>> Call();

 private:
  const std::list<Tracker*>::iterator trackers_it_;

  concurrent::Protected<Data> data_;
};

}  // namespace afc::infrastructure

#endif  // __AFC_EDITOR_SRC_TRACKERS_H__
