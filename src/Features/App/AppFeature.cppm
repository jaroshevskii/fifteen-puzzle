export module AppFeature;

import std;
import ComposableArchitecture;
import PuzzleFeature;

export namespace AppFeature {

struct State {
  PuzzleFeature::State puzzle;

  bool operator==(const State &) const = default;
};

// The root action domain wraps the puzzle's actions. Additional features would
// gain their own case here.
struct Puzzle {
  PuzzleFeature::Action action;
};

using Action = std::variant<Puzzle>;

State initialState();
ComposableArchitecture::Feature<State, Action> body();

} // namespace AppFeature

// --- Implementation
// -----------------------------------------------------------

namespace AppFeature {

State initialState() { return State{.puzzle = PuzzleFeature::initialState()}; }

// The root feature is a pure composition: it scopes the puzzle feature into the
// app domain. The puzzle's own onMount/onDismount run via the Scope.
ComposableArchitecture::Feature<State, Action> body() {
  return ComposableArchitecture::Scope<State, Action, PuzzleFeature::State,
                                       PuzzleFeature::Action>(
      &State::puzzle,
      ComposableArchitecture::casePath<Action, Puzzle, PuzzleFeature::Action>(
          &Puzzle::action),
      PuzzleFeature::body());
}

} // namespace AppFeature
