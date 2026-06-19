export module AppFeature;

import std;
import ComposableArchitecture;
import PuzzleFeature;

export namespace AppFeature {

struct State {
  PuzzleFeature::State puzzle;

  bool operator==(const State&) const = default;
};

// The root action domain wraps the puzzle's actions. Additional features would
// gain their own case here.
struct Puzzle {
  PuzzleFeature::Action action;
};

using Action = std::variant<Puzzle>;

using Effect = ComposableArchitecture::Effect<Action>;

State initialState();
ComposableArchitecture::ReducerFunction<State, Action> body();

}  // namespace AppFeature

// --- Implementation -----------------------------------------------------------

namespace AppFeature {

State initialState() {
  return State{.puzzle = PuzzleFeature::initialState()};
}

// The root reducer is a pure composition: it scopes the puzzle feature into the
// app domain.
ComposableArchitecture::ReducerFunction<State, Action> body() {
  return ComposableArchitecture::Scope<State, Action, PuzzleFeature::State, PuzzleFeature::Action>(
      &State::puzzle,
      ComposableArchitecture::casePath<Action, Puzzle, PuzzleFeature::Action>(&Puzzle::action),
      PuzzleFeature::body());
}

}  // namespace AppFeature
