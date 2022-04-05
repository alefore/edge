#include "src/audio.h"

#include <glog/logging.h>

namespace afc::editor {
struct AudioGenerator {
  std::function<AudioPlayer::GeneratorContinuation(AudioPlayer::Time,
                                                   AudioPlayer::SpeakerValue*)>
      callback;
};

namespace {
#if HAVE_LIBAO
class Frame {
 public:
  Frame(int size)
      : size_(size), buffer_(static_cast<char*>(calloc(size_, sizeof(char)))) {
    memset(buffer_, 0, size_);
  }

  ~Frame() { free(buffer_); }

  const char* buffer() const { return buffer_; }
  size_t size() const { return size_; }

  void Set(int position, int value) {
    buffer_[4 * position] = buffer_[4 * position + 2] = value & 0xff;
    buffer_[4 * position + 1] = buffer_[4 * position + 3] = (value >> 8) & 0xff;
  }

  void Add(int position, int value) { Set(position, value + Get(position)); }

  int Get(size_t position) {
    unsigned char low = static_cast<unsigned char>(buffer_[4 * position]);
    int high = (buffer_[4 * position + 1]);
    return (high << 8) + low;
  }

 private:
  const size_t size_;
  char* const buffer_;
};

class AudioPlayerImpl : public AudioPlayer {
 public:
  AudioPlayerImpl(ao_device* device, ao_sample_format format)
      : device_(device),
        format_(std::move(format)),
        empty_frame_(NewFrame()),
        background_thread_([this]() { PlayAudio(); }) {}

  ~AudioPlayerImpl() override {
    std::unique_lock<std::mutex> lock(mutex_);
    shutting_down_ = true;
    lock.unlock();
    background_thread_.join();
    ao_close(device_);
    ao_shutdown();
  }

  class AudioPlayerImplLock : public AudioPlayer::Lock {
   public:
    AudioPlayerImplLock(std::mutex* mutex, double* clock,
                        std::vector<AudioGenerator>* generators)
        : lock_(*mutex), clock_(clock), generators_(generators) {}

    void Add(AudioGenerator generator) override {
      generators_->push_back(std::move(generator));
    }

    double time() const override { return *clock_; }

   private:
    std::unique_lock<std::mutex> lock_;
    double* clock_;
    std::vector<AudioGenerator>* generators_;
  };

  std::unique_ptr<AudioPlayer::Lock> lock() override {
    return std::make_unique<AudioPlayerImplLock>(&mutex_, &time_, &generators_);
  }

 private:
  std::unique_ptr<Frame> NewFrame() {
    return std::make_unique<Frame>(frame_length_ * format_.bits / 8 *
                                   format_.channels * format_.rate);
  }

  void PlayAudio() {
    while (PlayNextFrame()) { /* Pass. */
    }
  }

  bool PlayNextFrame() {
    std::unique_ptr<Frame> new_frame;
    int iterations = frame_length_ * format_.rate;
    double delta = 1.0 / format_.rate;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (shutting_down_) {
        return false;
      }
      bool clean_up_needed = false;
      if (!generators_.empty()) {  // Optimization.
        new_frame = NewFrame();
        for (int i = 0; i < iterations; i++, time_ += delta) {
          for (auto& generator : generators_) {
            if (generator.callback != nullptr) {
              SpeakerValue output = 0;
              if (generator.callback(time_, &output) == STOP) {
                generator.callback = nullptr;
                clean_up_needed = true;
              }
              new_frame->Add(i, output);
            }
          }
        }
        if (clean_up_needed) {
          std::vector<AudioGenerator> next_generators;
          for (auto& generator : generators_) {
            if (generator.callback != nullptr) {
              next_generators.push_back(std::move(generator));
            }
          }
          generators_.swap(next_generators);
        }
      }
    }
    auto& frame = new_frame == nullptr ? *empty_frame_ : *new_frame;
    ao_play(device_, const_cast<char*>(frame.buffer()), frame.size());
    return true;
  }

  const double frame_length_ = 0.01;
  ao_device* const device_;
  const ao_sample_format format_;
  const std::unique_ptr<Frame> empty_frame_;

  std::vector<AudioGenerator> generators_;
  double time_ = 0.0;
  mutable std::mutex mutex_;

  bool shutting_down_ = false;
  std::thread background_thread_;
};
#endif

class NullAudioPlayer : public AudioPlayer {
  class NullLock : public AudioPlayer::Lock {
    double time() const override { return 0; }
    void Add(AudioGenerator) override {}
  };

  std::unique_ptr<AudioPlayer::Lock> lock() override {
    return std::make_unique<NullLock>();
  }
};
}  // namespace

std::unique_ptr<AudioPlayer> NewNullAudioPlayer() {
  return std::make_unique<NullAudioPlayer>();
}

std::unique_ptr<AudioPlayer> NewAudioPlayer() {
#if HAVE_LIBAO
  ao_initialize();
  ao_sample_format format;
  memset(&format, 0, sizeof(format));
  format.bits = 16;
  format.channels = 2;
  format.rate = 44100;
  format.byte_format = AO_FMT_LITTLE;

  ao_device* device = ao_open_live(ao_default_driver_id(), &format, nullptr);
  if (device == NULL) {
    fprintf(stderr, "Error opening device.\n");
    return NewNullAudioPlayer();
  }
  return std::make_unique<AudioPlayerImpl>(device, format);
#else
  return NewNullAudioPlayer();
#endif
}

AudioGenerator Smooth(double weight, int end_interval_samples,
                      AudioGenerator generator) {
  struct Data {
    double mean = 0;
    AudioPlayer::GeneratorContinuation generator_continuation =
        AudioPlayer::CONTINUE;
    int done_cycles = 0;
    AudioGenerator generator;
  };

  auto data = std::make_shared<Data>();
  data->generator = std::move(generator);
  return {.callback = [weight, end_interval_samples, data](
                          AudioPlayer::Time time,
                          AudioPlayer::SpeakerValue* output) {
    AudioPlayer::SpeakerValue tmp = 0;
    if (data->generator_continuation == AudioPlayer::STOP) {
      data->done_cycles++;
    } else {
      data->generator_continuation = data->generator.callback(time, &tmp);
    }
    data->mean = data->mean * (1.0 - weight) + tmp * weight;
    *output = static_cast<int>(data->mean);
    return data->done_cycles > end_interval_samples ? AudioPlayer::STOP
                                                    : AudioPlayer::CONTINUE;
  }};
}

void GenerateBeep(AudioPlayer& audio_player, AudioPlayer::Frequency frequency) {
  VLOG(5) << "Generating Beep";
  auto lock = audio_player.lock();
  AudioPlayer::Time start = lock->time();
  AudioPlayer::Duration duration = 0.1;
  lock->Add(
      ApplyVolume(SmoothVolume(0.3, start, start + duration, duration / 4),
                  Expiration(start + duration, Oscillate(frequency))));
}

void BeepFrequencies(AudioPlayer& audio_player, AudioPlayer::Duration duration,
                     const std::vector<AudioPlayer::Frequency>& frequencies) {
  auto lock = audio_player.lock();
  for (size_t i = 0; i < frequencies.size(); i++) {
    AudioPlayer::Time start = lock->time() + i * duration;
    lock->Add(
        ApplyVolume(SmoothVolume(0.3, start, start + duration, duration / 4),
                    Expiration(start + duration, Oscillate(frequencies[i]))));
  }
}

void GenerateAlert(AudioPlayer& audio_player) {
  VLOG(5) << "Generating Beep";
  BeepFrequencies(audio_player, 0.1, {523.25, 659.25, 783.99});
}

AudioGenerator Oscillate(AudioPlayer::Frequency freq) {
  return {.callback = [freq](AudioPlayer::Time time,
                             AudioPlayer::SpeakerValue* output) {
    *output = (int)(32768.0 * sin(2 * M_PI * freq * time));
    return AudioPlayer::CONTINUE;
  }};
}

AudioGenerator ApplyVolume(
    std::function<AudioPlayer::Volume(AudioPlayer::Time)> volume,
    AudioGenerator generator) {
  return {.callback = [volume, generator](AudioPlayer::Time time,
                                          AudioPlayer::SpeakerValue* output) {
    AudioPlayer::SpeakerValue tmp;
    auto result = generator.callback(time, &tmp);
    *output = tmp * volume(time);
    return result;
  }};
}

std::function<double(double)> SmoothVolume(double volume, double start,
                                           double end, double smooth_interval) {
  return [volume, start, end, smooth_interval](double time) {
    if (time < start || time > end) {
      return 0.0;
    } else if (time < start + smooth_interval) {
      return volume * (time - start) / smooth_interval;
    } else if (time >= end - smooth_interval) {
      return volume * (end - time) / smooth_interval;
    }
    return volume;
  };
}

AudioGenerator ApplyVolume(AudioPlayer::Volume volume,
                           AudioGenerator generator) {
  return ApplyVolume([volume](AudioPlayer::Time) { return volume; },
                     std::move(generator));
}

AudioGenerator Expiration(double expiration, AudioGenerator delegate) {
  return {
      .callback = [expiration, delegate](AudioPlayer::Time time,
                                         AudioPlayer::SpeakerValue* output) {
        return delegate.callback(time, output) == AudioPlayer::CONTINUE &&
                       time < expiration
                   ? AudioPlayer::CONTINUE
                   : AudioPlayer::STOP;
      }};
}

}  // namespace afc::editor
