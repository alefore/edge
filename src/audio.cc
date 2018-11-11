#include "src/audio.h"

#include <glog/logging.h>

namespace afc {
namespace editor {

namespace {
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

  void Add(int position, int value) {
    Set(position, value + Get(position));
  }

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
                        std::vector<Generator>* generators)
        : lock_(*mutex),
          clock_(clock),
          generators_(generators) {}

    void Add(Generator generator) override {
      generators_->push_back(std::move(generator));
    }

    double time() const override {
      return *clock_;
    }

   private:
    std::unique_lock<std::mutex> lock_;
    double* clock_;
    std::vector<Generator>* generators_;
  };

  std::unique_ptr<AudioPlayer::Lock> lock() override {
    return std::make_unique<AudioPlayerImplLock>(
        &mutex_, &time_, &generators_);
  }

 private:
  std::unique_ptr<Frame> NewFrame() {
    return std::make_unique<Frame>(
        frame_length_ * format_.bits / 8 * format_.channels * format_.rate);
  }

  void PlayAudio() {
    while (PlayNextFrame()) { /* Pass. */ }
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
            if (generator != nullptr) {
              int output = 0;
              if (generator(time_, &output) == STOP) {
                generator = nullptr;
                clean_up_needed = true;
              }
              new_frame->Add(i, output);
            }
          }
        }
        if (clean_up_needed) {
          std::vector<Generator> next_generators;
          for (auto& generator : generators_) {
            if (generator != nullptr) {
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

  std::vector<Generator> generators_;
  double time_ = 0.0;
  mutable std::mutex mutex_;

  bool shutting_down_ = false;
  std::thread background_thread_;
};

class NullAudioPlayer : public AudioPlayer {
  class NullLock : public AudioPlayer::Lock {
    double time() const override { return 0; }
    void Add(Generator) override {}
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
}

AudioPlayer::Generator Smooth(double weight, int end_interval_samples,
                              AudioPlayer::Generator generator) {
  struct Data {
    double mean = 0;
    AudioPlayer::GeneratorContinuation generator_continuation =
        AudioPlayer::CONTINUE;
    int done_cycles = 0;
    AudioPlayer::Generator generator;
  };

  auto data = std::make_shared<Data>();
  data->generator = std::move(generator);
  return [weight, end_interval_samples, data](double time, int* output) {
    int tmp = 0;
    if (data->generator_continuation == AudioPlayer::STOP) {
      data->done_cycles++;
    } else {
      data->generator_continuation = data->generator(time, &tmp);
    }
    data->mean = data->mean * (1.0 - weight) + tmp * weight;
    *output = static_cast<int>(data->mean);
    return data->done_cycles > end_interval_samples
               ? AudioPlayer::STOP : AudioPlayer::CONTINUE;
  };
}

void GenerateBeep(AudioPlayer* audio_player, double frequency) {
  VLOG(5) << "Generating Beep";
  auto lock = audio_player->lock();
  double start = lock->time();
  double duration = 0.1;
  lock->Add(Volume(SmoothVolume(0.3, start, start + duration, 0.01),
                   Expiration(start + duration, Frequency(frequency))));
}

void BeepFrequencies(AudioPlayer* audio_player,
                     const std::vector<double>& frequencies) {
  auto lock = audio_player->lock();
  for (size_t i = 0; i < frequencies.size(); i++) {
    double start = lock->time() + i * 0.1;
    lock->Add(Volume(SmoothVolume(0.3, start, start + 0.03, 0.01),
                     Expiration(start + 0.03, Frequency(frequencies[i]))));
  }
}

void GenerateAlert(AudioPlayer* audio_player) {
  VLOG(5) << "Generating Beep";
  BeepFrequencies(audio_player, { 523.25, 659.25, 783.99 });
}

AudioPlayer::Generator Frequency(double freq) {
  return [freq](double time, int* output) {
    *output = (int)(32768.0 * sin(2 * M_PI * freq * time));
    return AudioPlayer::CONTINUE;
  };
}

AudioPlayer::Generator Volume(std::function<double(double)> volume,
                         AudioPlayer::Generator generator) {
  return [volume, generator](double time, int* output) {
    int tmp;
    auto result = generator(time, &tmp);
    *output = tmp * volume(time);
    return result;
  };
}

std::function<double(double)> SmoothVolume(
    double volume, double start, double end, double smooth_interval) {
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

AudioPlayer::Generator Volume(double volume, AudioPlayer::Generator generator) {
  return Volume([volume](double) { return volume; },
                std::move(generator));
}

AudioPlayer::Generator Expiration(double expiration, AudioPlayer::Generator delegate) {
  return [expiration, delegate](double time, int* output) {
    return delegate(time, output) == AudioPlayer::CONTINUE && time < expiration
        ? AudioPlayer::CONTINUE : AudioPlayer::STOP;
  };
}

}  // namespace editor
}  // namespace afc
