#include "src/infrastructure/tracker.h"

#include <glog/logging.h>

#include "src/infrastructure/time.h"
#include "src/language/container.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"

namespace container = afc::language::container;

using afc::concurrent::EmptyValidator;
using afc::concurrent::Protected;
using afc::language::NonNull;

namespace afc::infrastructure {
namespace {
using Trackers = std::list<NonNull<Tracker*>>;

Protected<Trackers>::Lock lock_trackers() {
  static Protected<Trackers, EmptyValidator<Trackers>, false>* const output =
      new Protected<Trackers, EmptyValidator<Trackers>, false>();
  return output->lock();
}
}  // namespace

/* static */ std::list<Tracker::Data> Tracker::GetData() {
  std::list<Tracker::Data> output = container::MaterializeList(
      *lock_trackers() |
      std::views::transform(
          [](const NonNull<Tracker*> tracker) -> Tracker::Data {
            return *tracker->data_.lock();
          }));
  output.sort([](const Tracker::Data& a, const Tracker::Data& b) {
    return a.seconds < b.seconds;
  });
  return output;
}

/* static */ void Tracker::ResetAll() {
  // An alternative implementation would simply set the list to an empty list.
  // But that would require trackers to be able to handle destruction while the
  // objects returned by `Call` are still alive, which seems more complex.
  std::ranges::for_each(*lock_trackers(),
                        [](NonNull<Tracker*> tracker) { tracker->Reset(); });
}

Tracker::Tracker(std::wstring name)
    : trackers_it_([this]() {
        auto lock = lock_trackers();
        lock->push_front(NonNull<Tracker*>::AddressOf(*this));
        return lock->begin();
      }()),
      data_(Tracker::Data{std::move(name), 0, 0.0}) {}

Tracker::~Tracker() {
  LOG(FATAL) << "Internal error: afc::infrastructure::Tracker instances should "
                "never be deleted: "
             << data_.lock([](Data& data) { return data.name; });
}

std::unique_ptr<bool, std::function<void(bool*)>> Tracker::Call() {
  data_.lock([](Data& data) {
    VLOG(5) << "Start: " << data.name;
    data.executions++;
  });
  struct timespec start = infrastructure::Now();
  return std::unique_ptr<bool, std::function<void(bool*)>>(
      new bool(), [this, start](bool* value) {
        double seconds = GetElapsedSecondsSince(start);
        delete value;
        data_.lock([seconds](Data& data) {
          VLOG(6) << "Finish: " << data.name << ": " << seconds;
          data.seconds += seconds;
          data.longest_seconds = std::max(data.longest_seconds, seconds);
        });
      });
}

void Tracker::Reset() {
  data_.lock([](Data& data) {
    data.executions = 0;
    data.seconds = 0;
    data.longest_seconds = 0;
  });
}
}  // namespace afc::infrastructure
