#include "src/tracker.h"

#include "src/time.h"

namespace afc::editor {
namespace {
auto* const trackers = new std::list<Tracker*>();
}

/* static */ std::list<Tracker::Data> Tracker::GetData() {
  std::list<Tracker::Data> output;
  for (const auto* tracker : *trackers) {
    output.push_back(tracker->data_);
  }
  return output;
}

Tracker::Tracker(std::wstring name)
    : trackers_it_([this]() {
        trackers->push_front(this);
        return trackers->begin();
      }()),
      data_(Tracker::Data{std::move(name), 0, 0.0}) {}

Tracker::~Tracker() { trackers->erase(trackers_it_); }

std::unique_ptr<bool, std::function<void(bool*)>> Tracker::Call() {
  data_.executions++;
  struct timespec start;
  if (clock_gettime(0, &start) == -1) {
    return nullptr;
  }

  return std::unique_ptr<bool, std::function<void(bool*)>>(
      new bool(), [this, start](bool* value) {
        delete value;
        data_.seconds += GetElapsedSecondsSince(start);
      });
}
}  // namespace afc::editor
