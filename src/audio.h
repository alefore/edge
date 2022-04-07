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

namespace afc::editor {
struct AudioGenerator;

namespace audio {
GHOST_TYPE(Frequency, double, value);

class Player {
 public:
  using Time = double;
  using Duration = double;
  using Volume = double;
  using SpeakerValue = double;

  class Lock {
   public:
    virtual ~Lock() {}
    virtual Time time() const = 0;
    virtual void Add(AudioGenerator) = 0;
  };

  virtual ~Player() {}
  virtual std::unique_ptr<Lock> lock() = 0;
};

std::unique_ptr<Player> NewPlayer();
std::unique_ptr<Player> NewNullPlayer();
}  // namespace audio

using AudioPlayer = audio::Player;

void GenerateBeep(AudioPlayer& audio_player, audio::Frequency frequency);
void GenerateAlert(AudioPlayer& audio_player);
void BeepFrequencies(AudioPlayer& audio_player, AudioPlayer::Duration duration,
                     const std::vector<audio::Frequency>& frequencies);

AudioGenerator ApplyVolume(
    std::function<AudioPlayer::Volume(AudioPlayer::Time)> volume,
    AudioGenerator generator);
std::function<AudioPlayer::Volume(AudioPlayer::Time)> SmoothVolume(
    AudioPlayer::Volume baseline, AudioPlayer::Time start,
    AudioPlayer::Time end, double smooth_interval);
AudioGenerator Volume(AudioPlayer::Volume volume, AudioGenerator generator);

}  // namespace afc::editor

GHOST_TYPE_HASH(afc::editor::audio::Frequency, value);

#endif  // __AFC_EDITOR_AUDIO_H__
