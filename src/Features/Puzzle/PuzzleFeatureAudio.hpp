#pragma once

#include "Features/Puzzle/PuzzleFeature.hpp"

struct ALCcontext;
struct ALCdevice;

class PuzzleFeatureAudio {
 public:
  PuzzleFeatureAudio();
  ~PuzzleFeatureAudio();

  PuzzleFeatureAudio(const PuzzleFeatureAudio&) = delete;
  PuzzleFeatureAudio& operator=(const PuzzleFeatureAudio&) = delete;

  PuzzleFeatureAudio(PuzzleFeatureAudio&&) = delete;
  PuzzleFeatureAudio& operator=(PuzzleFeatureAudio&&) = delete;

  void update(bool isEnabled, const PuzzleFeature::State& state, double nowSeconds);

 private:
  bool wasEnabled_ = false;
  double observedStartTime_ = 0.0;
  int lastSecond_ = 0;

  ALCcontext* context_ = nullptr;
  ALCdevice* device_ = nullptr;

  unsigned int buffer_ = 0;
  unsigned int source_ = 0;
};
