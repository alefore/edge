#ifndef __AFC_EDITOR_DECAYING_COUNTER_H__
#define __AFC_EDITOR_DECAYING_COUNTER_H__

#include "src/infrastructure/time.h"

namespace afc {
namespace editor {

class DecayingCounter {
 public:
  DecayingCounter(double half_life_seconds);

  double GetEventsPerSecond() const;
  double IncrementAndGetEventsPerSecond(double events);

 private:
  const double half_life_seconds_;
  const double rate_scale_factor_;

  mutable struct timespec last_decay_ = {0, 0};
  mutable double scaled_rate_ = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_DECAYING_COUNTER_H__
