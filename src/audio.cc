#include "src/audio.h"

#include <glog/logging.h>

#include "src/protected.h"

namespace afc::editor {

struct AudioGenerator {
  using Callback = std::function<AudioPlayer::SpeakerValue(AudioPlayer::Time)>;
  Callback callback;
  AudioPlayer::Time start_time;
  AudioPlayer::Time end_time;
};

namespace {
AudioGenerator ApplyVolume(
    std::function<AudioPlayer::Volume(AudioPlayer::Time)> volume,
    AudioGenerator generator) {
  generator.callback = [volume,
                        callback = generator.callback](AudioPlayer::Time time) {
    return callback(time) * volume(time);
  };
  return generator;
}

std::function<AudioPlayer::Volume(AudioPlayer::Time)> SmoothVolume(
    AudioPlayer::Volume volume, AudioPlayer::Time start, AudioPlayer::Time end,
    double smooth_interval) {
  return [volume, start, end, smooth_interval](AudioPlayer::Time time) {
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
}  // namespace

#if HAVE_LIBAO
namespace {
AudioGenerator::Callback Oscillate(audio::Frequency freq) {
  return [freq](AudioPlayer::Time time) {
    return (int)(32768.0 * sin(2 * M_PI * freq.read() * time));
  };
}
}  // namespace

namespace audio {
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
  struct MutableData {
    std::vector<AudioGenerator> generators;
    // We gradually adjust the volume depending on the number of enabled
    // generators. This roughly assumes that a generator's volume is constant as
    // long as it's enabled.
    AudioPlayer::Volume volume = 1.0;
    AudioPlayer::Time time = 0.0;
    bool shutting_down = false;
  };

 public:
  AudioPlayerImpl(ao_device* device, ao_sample_format format)
      : device_(device),
        format_(std::move(format)),
        empty_frame_(NewFrame()),
        background_thread_([this]() { PlayAudio(); }) {}

  ~AudioPlayerImpl() override {
    data_.lock([](MutableData& data) { data.shutting_down = true; });

    background_thread_.join();
    ao_close(device_);
    ao_shutdown();
  }

  class AudioPlayerImplLock : public AudioPlayer::Lock {
   public:
    AudioPlayerImplLock(Protected<MutableData>::Lock data)
        : data_(std::move(data)) {}

    void Add(AudioGenerator generator) override {
      LOG(INFO) << "Adding generator: " << data_->generators.size();
      data_->generators.push_back(std::move(generator));
    }

    double time() const override { return data_->time; }

   private:
    Protected<MutableData>::Lock data_;
  };

  std::unique_ptr<AudioPlayer::Lock> lock() override {
    return std::make_unique<AudioPlayerImplLock>(data_.lock());
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
      Protected<MutableData>::Lock data = data_.lock();
      if (data->shutting_down) return false;
      CHECK_LT(data->generators.size(), 100ul);
      std::vector<AudioGenerator*> enabled_generators;
      for (auto& generator : data->generators)
        if (generator.start_time <= data->time)
          enabled_generators.push_back(&generator);

      if (!enabled_generators.empty()) {  // Optimization.
        new_frame = NewFrame();
        for (int i = 0; i < iterations; i++, data->time += delta) {
          data->volume =
              0.8 * data->volume + 0.2 * (1.0 / enabled_generators.size());
          for (auto& generator : enabled_generators)
            new_frame->Add(i, generator->callback(data->time) * data->volume);
        }
      } else if (!data->generators.empty()) {
        data->time += iterations * delta;
      }

      std::vector<AudioGenerator> next_generators;
      for (auto& generator : data->generators)
        if (generator.end_time > data->time)
          next_generators.push_back(std::move(generator));

      data->generators.swap(next_generators);
      if (data->generators.empty()) data->time = 0.0;
    }
    auto& frame = new_frame == nullptr ? *empty_frame_ : *new_frame;
    ao_play(device_, const_cast<char*>(frame.buffer()), frame.size());
    return true;
  }

  const double frame_length_ = 0.01;
  ao_device* const device_;
  const ao_sample_format format_;
  const std::unique_ptr<Frame> empty_frame_;

  Protected<MutableData> data_;

  std::thread background_thread_;
};
}  // namespace audio
#endif

namespace audio {
namespace {
class NullPlayer : public Player {
  class NullLock : public Player::Lock {
    double time() const override { return 0; }
    void Add(AudioGenerator) override {}
  };

  std::unique_ptr<Player::Lock> lock() override {
    return std::make_unique<NullLock>();
  }
};
}  // namespace

std::unique_ptr<AudioPlayer> NewNullPlayer() {
  return std::make_unique<NullPlayer>();
}

std::unique_ptr<AudioPlayer> NewPlayer() {
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
    return NewNullPlayer();
  }
  return std::make_unique<AudioPlayerImpl>(device, format);
#else
  return NewNullPlayer();
#endif
}

void GenerateBeep(Player& player, Frequency frequency) {
  VLOG(5) << "Generating Beep";
  auto lock = player.lock();
  Player::Time start = lock->time();
  Player::Duration duration = 0.1;
  lock->Add(
      ApplyVolume(SmoothVolume(0.3, start, start + duration, duration / 4),
                  {.callback = Oscillate(frequency),
                   .start_time = start,
                   .end_time = start + duration}));
}

void BeepFrequencies(Player& player, Player::Duration duration,
                     const std::vector<Frequency>& frequencies) {
  auto lock = player.lock();
  for (size_t i = 0; i < frequencies.size(); i++) {
    Player::Time start = lock->time() + i * duration;
    lock->Add(
        ApplyVolume(SmoothVolume(0.3, start, start + duration, duration / 4),
                    {.callback = Oscillate(frequencies[i]),
                     .start_time = start,
                     .end_time = start + duration}));
  }
}

void GenerateAlert(Player& player) {
  VLOG(5) << "Generating Beep";
  BeepFrequencies(player, 0.1,
                  {Frequency(523.25), Frequency(659.25), Frequency(783.99)});
}

}  // namespace audio

}  // namespace afc::editor
