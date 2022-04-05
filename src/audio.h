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
#include <mutex>
#include <thread>
#include <vector>

namespace afc::editor {

class AudioPlayer {
 public:
  enum GeneratorContinuation { STOP, CONTINUE };

  using Time = double;
  using Duration = double;
  using Frequency = double;
  using Volume = double;
  using SpeakerValue = double;
  using Generator = std::function<GeneratorContinuation(Time, SpeakerValue*)>;

  class Lock {
   public:
    virtual ~Lock() {}
    virtual Time time() const = 0;
    virtual void Add(Generator) = 0;
  };

  virtual ~AudioPlayer() {}
  virtual std::unique_ptr<Lock> lock() = 0;
};

std::unique_ptr<AudioPlayer> NewAudioPlayer();
std::unique_ptr<AudioPlayer> NewNullAudioPlayer();

void GenerateBeep(AudioPlayer& audio_player, AudioPlayer::Frequency frequency);
void GenerateAlert(AudioPlayer& audio_player);
void BeepFrequencies(AudioPlayer& audio_player, AudioPlayer::Duration duration,
                     const std::vector<double>& frequencies);

AudioPlayer::Generator Oscillate(AudioPlayer::Frequency freq);
AudioPlayer::Generator ApplyVolume(
    std::function<AudioPlayer::Volume(AudioPlayer::Time)> volume,
    AudioPlayer::Generator generator);
std::function<AudioPlayer::Volume(AudioPlayer::Time)> SmoothVolume(
    AudioPlayer::Volume baseline, AudioPlayer::Time start,
    AudioPlayer::Time end, double smooth_interval);
AudioPlayer::Generator Volume(AudioPlayer::Volume volume,
                              AudioPlayer::Generator generator);
AudioPlayer::Generator Expiration(AudioPlayer::Time expiration,
                                  AudioPlayer::Generator delegate);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_AUDIO_H__
