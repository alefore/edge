#include "src/tracker.h"

#include "src/time.h"

namespace afc::editor {
namespace {
using Trackers = std::list<Tracker*>;

Protected<Trackers>::Lock lock_trackers() {
  static Protected<Trackers>* const output = new Protected<Trackers>();
  return output->lock();
}
}  // namespace

/* static */ std::list<Tracker::Data> Tracker::GetData() {
  std::list<Tracker::Data> output;
  auto trackers = lock_trackers();
  for (const auto* tracker : *trackers) {
    output.push_back(*tracker->data_.lock());
  }
  return output;
}

Tracker::Tracker(std::wstring name)
    : trackers_it_([this]() {
        auto lock = lock_trackers();
        lock->push_front(this);
        return lock->begin();
      }()),
      data_(Tracker::Data{std::move(name), 0, 0.0}) {}

Tracker::~Tracker() { lock_trackers()->erase(trackers_it_); }

std::unique_ptr<bool, std::function<void(bool*)>> Tracker::Call() {
  data_.lock([](Data& data) { data.executions++; });

  struct timespec start;
  if (clock_gettime(0, &start) == -1) {
    return nullptr;
  }

  return std::unique_ptr<bool, std::function<void(bool*)>>(
      new bool(), [this, start](bool* value) {
        double seconds = GetElapsedSecondsSince(start);
        delete value;
        data_.lock([start, seconds](Data& data) { data.seconds += seconds; });
      });
}
}  // namespace afc::editor
