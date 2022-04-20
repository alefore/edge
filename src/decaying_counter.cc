#include "src/decaying_counter.h"

#include <glog/logging.h>

#include <cmath>
#include <memory>

#include "src/infrastructure/time.h"

namespace afc {
namespace editor {

double SecondsToMicroseconds(double seconds) { return seconds * 1e6; }

DecayingCounter::DecayingCounter(double half_life_seconds)
    : half_life_seconds_(half_life_seconds),
      rate_scale_factor_(half_life_seconds_ * sqrt(2)) {}

double DecayingCounter::GetEventsPerSecond() const {
  return const_cast<DecayingCounter*>(this)->IncrementAndGetEventsPerSecond(
      0.0);
}

double DecayingCounter::IncrementAndGetEventsPerSecond(double events) {
  const double elapsed_half_lifes =
      GetElapsedSecondsAndUpdate(&last_decay_) / half_life_seconds_;
  if (elapsed_half_lifes > 0) {
    const double decay_factor = std::exp2(-elapsed_half_lifes);
    VLOG(5) << "Decaying. Factor: " << decay_factor
            << ", previous: " << scaled_rate_ << ", events: " << events
            << ", elapsed half lifes: " << elapsed_half_lifes;
    CHECK_GE(decay_factor, 0.0);
    CHECK_LE(decay_factor, 1.0);
    scaled_rate_ = scaled_rate_ * decay_factor;
  }
  scaled_rate_ += events;
  return scaled_rate_ / rate_scale_factor_;
}

}  // namespace editor
}  // namespace afc
