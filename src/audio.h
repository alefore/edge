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

  using Generator = std::function<GeneratorContinuation(double, int*)>;

  class Lock {
   public:
    virtual ~Lock() {}
    virtual double time() const = 0;
    virtual void Add(Generator) = 0;
  };

  virtual ~AudioPlayer() {}
  virtual std::unique_ptr<Lock> lock() = 0;
};

std::unique_ptr<AudioPlayer> NewAudioPlayer();
std::unique_ptr<AudioPlayer> NewNullAudioPlayer();

void GenerateBeep(AudioPlayer* audio_player, double frequency);
void GenerateAlert(AudioPlayer* audio_player);
void BeepFrequencies(AudioPlayer* audio_player,
                     const std::vector<double>& frequencies);

AudioPlayer::Generator Frequency(double freq);
AudioPlayer::Generator Volume(std::function<double(double)> volume,
                              AudioPlayer::Generator generator);
std::function<double(double)> SmoothVolume(double baseline, double start,
                                           double end, double smooth_interval);
AudioPlayer::Generator Volume(double volume, AudioPlayer::Generator generator);
AudioPlayer::Generator Expiration(double expiration,
                                  AudioPlayer::Generator delegate);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_AUDIO_H__
