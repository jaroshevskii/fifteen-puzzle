module;

// C audio backend headers belong in the global module fragment; they are an
// implementation detail and are not exported to importers.
#include <AL/al.h>
#include <AL/alc.h>

export module AudioPlayerClientLive;

import std;
import AudioPlayerClient;

// Live, OpenAL-backed implementation of the audio player dependency. Wire it in
// at app launch:
//
//   prepareDependencies([](DependencyValues& values) {
//     values.set<AudioPlayerClient::Key>(AudioPlayerClient::live());
//   });
export namespace AudioPlayerClient {

// Returns a client whose sounds are synthesized and played through OpenAL. The
// returned client owns the underlying audio engine via shared ownership, so the
// device stays open for as long as any copy of the client is alive.
Client live();

}  // namespace AudioPlayerClient

namespace AudioPlayerClient {

namespace {

// Owns the OpenAL device, context, and the single synthesized "tick" buffer.
class Engine {
 public:
  Engine() {
    device_ = alcOpenDevice(nullptr);
    if (!device_) {
      return;
    }

    context_ = alcCreateContext(device_, nullptr);
    if (!context_) {
      alcCloseDevice(device_);
      device_ = nullptr;
      return;
    }
    alcMakeContextCurrent(context_);

    constexpr int sampleRate = 44100;
    constexpr float beepDuration = 0.08f;
    constexpr float beepFrequency = 1200.0f;

    const int sampleCount = static_cast<int>(beepDuration * static_cast<float>(sampleRate));
    std::vector<ALshort> samples(sampleCount);

    for (int index = 0; index < sampleCount; ++index) {
      const double t = static_cast<double>(index) / static_cast<double>(sampleRate);
      double value = std::sin(2.0 * std::numbers::pi_v<double> * static_cast<double>(beepFrequency) * t);
      value *= std::exp(-t / (static_cast<double>(beepDuration) * 0.25));
      samples[index] = static_cast<ALshort>(value * 22000.0);
    }

    alGenBuffers(1, &buffer_);
    alBufferData(buffer_, AL_FORMAT_MONO16, samples.data(), sampleCount * static_cast<int>(sizeof(ALshort)), sampleRate);

    alGenSources(1, &source_);
    alSourcei(source_, AL_BUFFER, static_cast<int>(buffer_));
    alSourcef(source_, AL_GAIN, 0.75f);
    alSourcei(source_, AL_LOOPING, AL_FALSE);
  }

  ~Engine() {
    if (source_ != 0) {
      alDeleteSources(1, &source_);
    }
    if (buffer_ != 0) {
      alDeleteBuffers(1, &buffer_);
    }
    alcMakeContextCurrent(nullptr);
    if (context_) {
      alcDestroyContext(context_);
    }
    if (device_) {
      alcCloseDevice(device_);
    }
  }

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  void playTick() {
    if (source_ != 0) {
      alSourcePlay(source_);
    }
  }

 private:
  ALCcontext* context_ = nullptr;
  ALCdevice* device_ = nullptr;
  unsigned int buffer_ = 0;
  unsigned int source_ = 0;
};

}  // namespace

Client live() {
  auto engine = std::make_shared<Engine>();
  return Client{[engine](Sound sound) {
    switch (sound) {
      case Sound::tick:
        engine->playTick();
        break;
    }
  }};
}

}  // namespace AudioPlayerClient
