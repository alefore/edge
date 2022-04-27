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

#include "src/language/ghost_type.h"
#include "src/language/safe_types.h"

namespace afc::editor::audio {
GHOST_TYPE_DOUBLE(Frequency);
GHOST_TYPE(SpeakerValue, int);
GHOST_TYPE_DOUBLE(Volume);

struct Generator;

class Player {
 public:
  using Time = double;
  using Duration = double;

  class Lock {
   public:
    virtual ~Lock() {}
    virtual Time time() const = 0;
    virtual void Add(Generator) = 0;
  };

  virtual ~Player() {}
  virtual language::NonNull<std::unique_ptr<Lock>> lock() = 0;
  virtual void SetVolume(Volume) = 0;
};

language::NonNull<std::unique_ptr<Player>> NewPlayer();
language::NonNull<std::unique_ptr<Player>> NewNullPlayer();

void GenerateBeep(Player& player, Frequency frequency);
void BeepFrequencies(Player& player, Player::Duration duration,
                     const std::vector<Frequency>& frequencies);
void GenerateAlert(Player& player);
}  // namespace afc::editor::audio

GHOST_TYPE_HASH(afc::editor::audio::Frequency);

#endif  // __AFC_EDITOR_AUDIO_H__
