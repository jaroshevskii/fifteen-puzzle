#pragma once

#include <functional>
#include <utility>
#include <variant>
#include <vector>

#include "Features/Puzzle/PuzzleFeature.hpp"

namespace AppFeature {

struct State {
  bool isTickSoundEnabled = false;
  PuzzleFeature::State puzzle;
};

struct AppLaunched {};
struct SoundToggleKeyPressed {};
struct PuzzleActionReceived {
  PuzzleFeature::Action action;
};

using Action = std::variant<AppLaunched, SoundToggleKeyPressed, PuzzleActionReceived>;

struct PuzzleEffectProduced {
  PuzzleFeature::Effect effect;
};

using Effect = std::variant<PuzzleEffectProduced>;
using Dispatch = std::function<void(const Action&)>;

State makeInitialState();
std::pair<State, std::vector<Effect>> reduce(const State& state, const Action& action, double nowSeconds);

}  // namespace AppFeature
