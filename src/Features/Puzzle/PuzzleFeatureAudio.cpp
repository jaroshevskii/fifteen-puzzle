#include "Features/Puzzle/PuzzleFeatureAudio.hpp"

#include <cmath>
#include <numbers>
#include <vector>

#include <AL/al.h>
#include <AL/alc.h>

PuzzleFeatureAudio::PuzzleFeatureAudio() {
  this->device_ = alcOpenDevice(nullptr);
  if (!this->device_) {
    return;
  }

  this->context_ = alcCreateContext(this->device_, nullptr);
  if (!this->context_) {
    alcCloseDevice(this->device_);
    this->device_ = nullptr;
    return;
  }
  alcMakeContextCurrent(this->context_);

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

  alGenBuffers(1, &this->buffer_);
  alBufferData(this->buffer_, AL_FORMAT_MONO16, samples.data(), sampleCount * static_cast<int>(sizeof(ALshort)), sampleRate);

  alGenSources(1, &this->source_);
  alSourcei(this->source_, AL_BUFFER, static_cast<int>(this->buffer_));
  alSourcef(this->source_, AL_GAIN, 0.75f);
  alSourcei(this->source_, AL_LOOPING, AL_FALSE);
}

PuzzleFeatureAudio::~PuzzleFeatureAudio() {
  if (this->source_ != 0) {
    alDeleteSources(1, &this->source_);
  }
  if (this->buffer_ != 0) {
    alDeleteBuffers(1, &this->buffer_);
  }

  alcMakeContextCurrent(nullptr);
  if (this->context_) {
    alcDestroyContext(this->context_);
  }
  if (this->device_) {
    alcCloseDevice(this->device_);
  }
}

void PuzzleFeatureAudio::update(bool isEnabled, const PuzzleFeature::State& state, double nowSeconds) {
  if (!isEnabled || this->source_ == 0 || !state.startTime.has_value()) {
    this->wasEnabled_ = isEnabled;
    return;
  }

  const double startTime = *state.startTime;
  const int currentSecond = static_cast<int>(nowSeconds - startTime);

  if (!this->wasEnabled_) {
    this->observedStartTime_ = startTime;
    this->lastSecond_ = currentSecond;
    this->wasEnabled_ = true;
    return;
  }

  if (startTime != this->observedStartTime_) {
    this->observedStartTime_ = startTime;
    this->lastSecond_ = currentSecond;
    return;
  }

  if (currentSecond > this->lastSecond_) {
    alSourcePlay(this->source_);
    this->lastSecond_ = currentSecond;
  }
}
