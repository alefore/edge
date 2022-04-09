#ifndef __AFC_EDITOR_AUDIO_H__
#define __AFC_EDITOR_AUDIO_H__

// clang-format off
#include "config.h"
// clang-format on

#if HAVE_LIBAO
#include <ao/ao.h>
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "src/ghost_type.h"

namespace afc::editor::audio {
GHOST_TYPE(Frequency, double);
GHOST_TYPE(SpeakerValue, int);

struct Generator;

class Player {
 public:
  using Time = double;
  using Duration = double;
  using Volume = double;

  class Lock {
   public:
    virtual ~Lock() {}
    virtual Time time() const = 0;
    virtual void Add(Generator) = 0;
  };

  virtual ~Player() {}
  virtual std::unique_ptr<Lock> lock() = 0;
};

std::unique_ptr<Player> NewPlayer();
std::unique_ptr<Player> NewNullPlayer();

void GenerateBeep(Player& player, Frequency frequency);
void BeepFrequencies(Player& player, Player::Duration duration,
                     const std::vector<Frequency>& frequencies);
void GenerateAlert(Player& player);
}  // namespace afc::editor::audio

GHOST_TYPE_HASH(afc::editor::audio::Frequency);

#endif  // __AFC_EDITOR_AUDIO_H__
