#include "src/tracker.h"

#include <mutex>

#include "src/time.h"

namespace afc::editor {
namespace {
std::mutex trackers_mutex;
auto* const trackers = new std::list<Tracker*>();
}  // namespace

/* static */ std::list<Tracker::Data> Tracker::GetData() {
  std::list<Tracker::Data> output;
  std::unique_lock<std::mutex> lock(trackers_mutex);
  for (const auto* tracker : *trackers) {
    output.push_back(tracker->data_);
  }
  return output;
}

Tracker::Tracker(std::wstring name)
    : trackers_it_([this]() {
        std::unique_lock<std::mutex> lock(trackers_mutex);
        trackers->push_front(this);
        return trackers->begin();
      }()),
      data_(Tracker::Data{std::move(name), 0, 0.0}) {}

Tracker::~Tracker() {
  std::unique_lock<std::mutex> lock(trackers_mutex);
  trackers->erase(trackers_it_);
}

std::unique_ptr<bool, std::function<void(bool*)>> Tracker::Call() {
  std::unique_lock<std::mutex> lock(trackers_mutex);
  data_.executions++;
  struct timespec start;
  if (clock_gettime(0, &start) == -1) {
    return nullptr;
  }

  return std::unique_ptr<bool, std::function<void(bool*)>>(
      new bool(), [this, start](bool* value) {
        delete value;
        std::unique_lock<std::mutex> lock(trackers_mutex);
        data_.seconds += GetElapsedSecondsSince(start);
      });
}
}  // namespace afc::editor
