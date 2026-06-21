// Tests for PuzzleFeature using the TestStore and overridden dependencies.
//
// Because randomness and time flow through controlled dependencies, the reducer
// is fully deterministic here: `withDependencies` pins the clock and seeds the
// random number generator, so shuffles and durations are reproducible.

import std;
import ComposableArchitecture;
import PuzzleFeature;
import SolverClient;

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
          state.moveHistory.push_back(15);              // the slide is recorded
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

        store.send(PuzzleFeature::TileTapped{0}, {});  // {} = expect no state change

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

// onMount shuffles the board (off the seeded generator) and starts the timer
// when the store is created — no AppLaunched action needed (TCA 2.0 lifecycle).
void testOnMountShufflesAndStartsTimer() {
  withDependencies(
      [](DependencyValues& values) {
        values.context = DependencyContext::test;
        values.set<DateGeneratorKey>(DateGenerator::constant(100.0));
        values.set<RandomNumberGeneratorKey>(RandomNumberGenerator::seeded(42));
      },
      [] {
        const auto solvedTiles = PuzzleFeature::initialState().tiles;

        // Constructing the store runs onMount, which shuffles and starts the timer.
        TestStore<PuzzleFeature::State, PuzzleFeature::Action> store(PuzzleFeature::initialState(), PuzzleFeature::body);

        expect(store.state().tiles != solvedTiles, "onMount shuffles the board");
        expect(store.state().startDate == 100.0, "onMount starts the timer at the controlled clock");
        expect(!store.failed(), "onMount produced no unexpected effects");
        return 0;
      });
}

// A SolverClient stub returning a fixed plan, so the async auto-solve flow is
// deterministic and thread-free under TestStore (effects run inline).
SolverClient::Client stubSolver(std::vector<int> plan) {
  return SolverClient::Client{.plan = [plan](std::vector<int>, int, std::stop_token) {
    return std::expected<std::vector<int>, SolverClient::SolveError>{plan};
  }};
}

// AutoSolve kicks off the (stubbed) solver, receives its moves, and animates
// them on the controlled clock until the board is solved.
void testAutoSolveAnimatesToSolved() {
  withDependencies(
      [](DependencyValues& values) {
        values.context = DependencyContext::test;
        values.set<DateGeneratorKey>(DateGenerator::constant(0.0));
        values.set<SolverClient::Key>(stubSolver({15}));  // one move solves almostSolvedState
      },
      [] {
        TestStore<PuzzleFeature::State, PuzzleFeature::Action> store(almostSolvedState(), PuzzleFeature::body);

        store.send(PuzzleFeature::AutoSolveButtonTapped{}, [](PuzzleFeature::State& state) { state.isSolving = true; });

        // The background task (run inline here) reports its solution.
        store.receive([](PuzzleFeature::State& state) {
          state.pendingMoves = {15};
          state.nextMoveAt = 0.0;
          state.solveInterval = 0.10;  // clamp(4.0 / 1 move)
        });

        // A tick at/after nextMoveAt plays the queued move, solving the board.
        store.send(PuzzleFeature::TimerTicked{}, [](PuzzleFeature::State& state) {
          std::swap(state.tiles[14], state.tiles[15]);
          state.moveHistory.push_back(15);
          state.pendingMoves.clear();
          state.nextMoveAt += 0.10;  // advanced by one interval
          state.isSolving = false;
          state.isGameOver = true;
          state.lastDuration = 0;
          state.startDate = std::nullopt;
        });

        expect(!store.failed(), "AutoSolve animates the board to solved");
        return 0;
      });
}

// Interacting mid-solve cancels the auto-solve.
void testInteractionCancelsAutoSolve() {
  withDependencies(
      [](DependencyValues& values) {
        values.context = DependencyContext::test;
        values.set<DateGeneratorKey>(DateGenerator::constant(0.0));
        values.set<RandomNumberGeneratorKey>(RandomNumberGenerator::seeded(7));
        values.set<SolverClient::Key>(stubSolver({15}));
      },
      [] {
        TestStore<PuzzleFeature::State, PuzzleFeature::Action> store(almostSolvedState(), PuzzleFeature::body);

        store.send(PuzzleFeature::AutoSolveButtonTapped{}, [](PuzzleFeature::State& state) { state.isSolving = true; });
        store.receive([](PuzzleFeature::State& state) {
          state.pendingMoves = {15};
          state.nextMoveAt = 0.0;
          state.solveInterval = 0.10;
        });

        // Shuffling interrupts the solve: solving stops and moves are dropped.
        store.send(PuzzleFeature::ShuffleButtonTapped{}, [&store](PuzzleFeature::State& state) {
          state.isSolving = false;
          state.pendingMoves.clear();
          state.tiles = store.state().tiles;              // reshuffled by the seeded generator
          state.moveHistory = store.state().moveHistory;  // history reset to the new scramble
        });

        expect(!store.failed(), "interaction cancels the auto-solve");
        return 0;
      });
}

}  // namespace

int main() {
  testTileTappedSwapsAdjacentTile();
  testTileTappedIgnoresNonAdjacentTile();
  testTimerTickedAdvancesElapsedSeconds();
  testOnMountShufflesAndStartsTimer();
  testAutoSolveAnimatesToSolved();
  testInteractionCancelsAutoSolve();

  if (failures == 0) {
    std::println("All PuzzleFeature tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} test(s) failed.", failures);
  return 1;
}
