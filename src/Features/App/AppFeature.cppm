export module AppFeature;

import std;
import ComposableArchitecture;
import Dependencies;
import PuzzleFeature;
import LeaderboardFeature;
import SharedModels;
import Sharing;
import AppSettings;

export namespace AppFeature {

struct State {
  PuzzleFeature::State puzzle;
  LeaderboardFeature::State leaderboard;
  // Edge-detects the win transition so a single completed game submits exactly
  // one score (re-armed when a new game starts).
  bool didSubmitCurrentWin = false;

  bool operator==(const State &) const = default;
};

// The root action domain wraps each child feature's actions.
struct Puzzle {
  PuzzleFeature::Action action;
};
struct Leaderboard {
  LeaderboardFeature::Action action;
};

using Action = std::variant<Puzzle, Leaderboard>;

State initialState(Sharing::Shared<AppSettings::Settings> settings = {});
ComposableArchitecture::Feature<State, Action> body();

} // namespace AppFeature

// --- Implementation
// -----------------------------------------------------------

namespace AppFeature {

State initialState(Sharing::Shared<AppSettings::Settings> settings) {
  auto puzzle = PuzzleFeature::initialState(std::move(settings));
  LeaderboardFeature::State leaderboard;
  leaderboard.gridSize = puzzle.grid; // show the current board's leaderboard
  return State{.puzzle = std::move(puzzle), .leaderboard = std::move(leaderboard)};
}

// The root feature composes the puzzle and leaderboard scopes, with a small
// glue body between them that turns a puzzle win into a leaderboard submission
// (PuzzleFeature stays unaware of the leaderboard, keeping the graph acyclic —
// the parent owns the cross-feature wiring, as in isowords).
ComposableArchitecture::Feature<State, Action> body() {
  using FeatureStore = ComposableArchitecture::Store<State, Action>;

  auto feature =
      ComposableArchitecture::Scope<State, Action, PuzzleFeature::State, PuzzleFeature::Action>(
          &State::puzzle,
          ComposableArchitecture::casePath<Action, Puzzle, PuzzleFeature::Action>(&Puzzle::action),
          PuzzleFeature::body());

  // Runs after the puzzle scope (order preserved by `add`), so the puzzle state
  // it inspects already reflects the action just processed.
  feature.add(ComposableArchitecture::Update<State, Action>(
      [](State &state, const Action &, FeatureStore &store) {
        if (state.puzzle.isGameOver && !state.didSubmitCurrentWin &&
            state.puzzle.lastDuration.has_value()) {
          state.didSubmitCurrentWin = true;
          Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
          SharedModels::ScoreSubmission submission{
              .name = state.puzzle.settings.get().playerName,
              .gridSize = state.puzzle.grid,
              .moves = static_cast<int>(state.puzzle.moveHistory.size()),
              .duration = *state.puzzle.lastDuration,
              .playedAt = date->now()};
          state.leaderboard.gridSize = state.puzzle.grid;
          store.send(Leaderboard{LeaderboardFeature::ScoreSubmitted{std::move(submission)}});
        } else if (!state.puzzle.isGameOver) {
          state.didSubmitCurrentWin = false; // re-arm for the next game
        }
      }));

  feature.add(ComposableArchitecture::Scope<State, Action, LeaderboardFeature::State,
                                            LeaderboardFeature::Action>(
      &State::leaderboard,
      ComposableArchitecture::casePath<Action, Leaderboard, LeaderboardFeature::Action>(
          &Leaderboard::action),
      LeaderboardFeature::body()));

  return feature;
}

} // namespace AppFeature
