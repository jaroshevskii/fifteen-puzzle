// Tests for PuzzleFeature using the TestStore and overridden dependencies.
//
// Because randomness and time flow through controlled dependencies, the reducer
// is fully deterministic here: `withDependencies` pins the clock and seeds the
// random number generator, so shuffles and durations are reproducible.

import std;
import ComposableArchitecture;
import PuzzleFeature;

using ComposableArchitecture::TestStore;
using Dependencies::DateGenerator;
using Dependencies::DateGeneratorKey;
using Dependencies::DependencyContext;
using Dependencies::DependencyValues;
using Dependencies::RandomNumberGenerator;
using Dependencies::RandomNumberGeneratorKey;
using Dependencies::withDependencies;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", message);
  }
}

// A solved board with the empty square swapped next to tile "15", so a single
// tap solves it. The empty cell is at index 14; tile "15" is at index 15.
PuzzleFeature::State almostSolvedState() {
  PuzzleFeature::State state = PuzzleFeature::initialState();  // solved board
  std::swap(state.tiles[14], state.tiles[15]);                // empty <-> "15"
  state.startDate = 0.0;
  return state;
}

// Tapping the empty-adjacent "15" tile slides it home and wins the game.
void testTileTappedSwapsAdjacentTile() {
  withDependencies(
      [](DependencyValues& values) { values.context = DependencyContext::test; },
      [] {
        TestStore<PuzzleFeature::State, PuzzleFeature::Action> store(almostSolvedState(), PuzzleFeature::body);

        store.send(PuzzleFeature::TileTapped{15}, [](PuzzleFeature::State& state) {
          std::swap(state.tiles[14], state.tiles[15]);  // board is now solved
          state.isGameOver = true;
          state.lastDuration = 0;  // clock pinned at 0
          state.startDate = std::nullopt;
        });

        expect(!store.failed(), "TileTapped swaps adjacent tile and wins");
        return 0;
      });
}

// Tapping a tile that is not adjacent to the empty square is a no-op.
void testTileTappedIgnoresNonAdjacentTile() {
  withDependencies(
      [](DependencyValues& values) { values.context = DependencyContext::test; },
      [] {
        // Empty square is at index 14; index 0 is not adjacent to it.
        TestStore<PuzzleFeature::State, PuzzleFeature::Action> store(almostSolvedState(), PuzzleFeature::body);

        store.send(PuzzleFeature::TileTapped{0});

        expect(!store.failed(), "TileTapped ignores non-adjacent tile");
        return 0;
      });
}

// The timer advances as the clock advances, tracked in state via `TimerTicked`.
void testTimerTickedAdvancesElapsedSeconds() {
  double now = 0.0;
  withDependencies(
      [&now](DependencyValues& values) {
        values.context = DependencyContext::test;
        values.set<DateGeneratorKey>(DateGenerator{[&now] { return now; }});
      },
      [&now] {
        // An unsolved board, so ticks advance the timer without winning.
        TestStore<PuzzleFeature::State, PuzzleFeature::Action> store(almostSolvedState(), PuzzleFeature::body);

        now = 2.5;
        store.send(PuzzleFeature::TimerTicked{}, [](PuzzleFeature::State& state) { state.secondsElapsed = 2; });

        now = 9.0;
        store.send(PuzzleFeature::TimerTicked{}, [](PuzzleFeature::State& state) { state.secondsElapsed = 9; });

        expect(!store.failed(), "TimerTicked advances elapsed seconds with the clock");
        return 0;
      });
}

// AppLaunched shuffles the board (off the seeded generator) and starts the timer
// by feeding a TimerStarted action back through the effect.
void testAppLaunchedShufflesAndStartsTimer() {
  withDependencies(
      [](DependencyValues& values) {
        values.context = DependencyContext::test;
        values.set<DateGeneratorKey>(DateGenerator::constant(100.0));
        values.set<RandomNumberGeneratorKey>(RandomNumberGenerator::seeded(42));
      },
      [] {
        TestStore<PuzzleFeature::State, PuzzleFeature::Action> store(PuzzleFeature::initialState(), PuzzleFeature::body);
        const auto solvedTiles = PuzzleFeature::initialState().tiles;

        store.send(PuzzleFeature::AppLaunched{}, [&store](PuzzleFeature::State& state) {
          state.tiles = store.state().tiles;  // shuffled by the seeded generator
        });
        expect(store.state().tiles != solvedTiles, "AppLaunched shuffles the board");

        // The start-timer effect feeds TimerStarted{100} back into the store.
        store.receive([](PuzzleFeature::State& state) {
          state.startDate = 100.0;
          state.secondsElapsed = 0;
        });

        expect(!store.failed(), "AppLaunched starts the timer via an effect");
        return 0;
      });
}

}  // namespace

int main() {
  testTileTappedSwapsAdjacentTile();
  testTileTappedIgnoresNonAdjacentTile();
  testTimerTickedAdvancesElapsedSeconds();
  testAppLaunchedShufflesAndStartsTimer();

  if (failures == 0) {
    std::println("All PuzzleFeature tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} test(s) failed.", failures);
  return 1;
}
