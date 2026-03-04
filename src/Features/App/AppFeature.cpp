#include "Features/App/AppFeature.hpp"

namespace AppFeature {

State makeInitialState() {
  return State{
      .isTickSoundEnabled = false,
      .puzzle = PuzzleFeature::makeInitialState()};
}

std::pair<State, std::vector<Effect>> reduce(const State& state, const Action& action, double nowSeconds) {
  State nextState = state;
  std::vector<Effect> effects;

  auto runPuzzleReducer = [&](const PuzzleFeature::Action& puzzleAction) {
    auto [puzzleState, puzzleEffects] = PuzzleFeature::reduce(nextState.puzzle, puzzleAction, nowSeconds);
    nextState.puzzle = std::move(puzzleState);
    for (const auto& puzzleEffect : puzzleEffects) {
      effects.push_back(PuzzleEffectProduced{puzzleEffect});
    }
  };

  std::visit(
      [&](auto&& value) {
        using Value = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<Value, AppLaunched>) {
          runPuzzleReducer(PuzzleFeature::AppLaunched{});
        } else if constexpr (std::is_same_v<Value, SoundToggleKeyPressed>) {
          nextState.isTickSoundEnabled = !nextState.isTickSoundEnabled;
        } else if constexpr (std::is_same_v<Value, PuzzleActionReceived>) {
          runPuzzleReducer(value.action);
        }
      },
      action);

  return {nextState, effects};
}

}  // namespace AppFeature
