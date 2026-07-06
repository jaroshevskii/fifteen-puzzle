module PuzzleFeature; // implementation unit

import std;
import ComposableArchitecture;
import AudioPlayerClient;
import PuzzleCore;
import SolverClient;
import Sharing;
import AppSettings;

namespace PuzzleFeature {

namespace {

// The board rules (solved layout, adjacency, scramble, slide) live in the
// shared PuzzleCore module, so the server can re-play multiplayer boards with
// the exact same logic (the PuzzleGen pattern from isowords).
using PuzzleCore::isSolved;
using PuzzleCore::neighbors;
using PuzzleCore::scramble;
using PuzzleCore::solvedTiles;

using Dependencies::RandomNumberGenerator;

constexpr std::string_view kSolverCancelId = "auto-solve";

void startNewGame(State &state, int grid, RandomNumberGenerator &rng) {
  state.grid = grid;
  state.isGameOver = false;
  state.lastDuration = std::nullopt;
  state.secondsElapsed = 0;
  state.startDate = std::nullopt;
  scramble(grid, rng, state.tiles, state.moveHistory, grid * grid * 10);
}

// Slides the tile at `pos` into the empty cell (if adjacent), recording the
// move.
bool applySlide(State &state, int pos) {
  return PuzzleCore::slide(state.tiles, state.moveHistory, state.grid, pos);
}

} // namespace

State initialState(Sharing::Shared<AppSettings::Settings> settings) {
  const int grid = std::clamp(settings.get().lastBoardSize, Config::minGrid, Config::maxGrid);
  return State{.grid = grid, .tiles = solvedTiles(grid), .settings = std::move(settings)};
}

int displayedSeconds(const State &state) {
  if (state.isGameOver && state.lastDuration.has_value()) {
    return *state.lastDuration;
  }
  return state.secondsElapsed;
}

std::optional<int> emptyIndex(const State &state) {
  const auto it = std::ranges::find(state.tiles, std::string{});
  if (it == state.tiles.end()) {
    return std::nullopt;
  }
  return static_cast<int>(std::ranges::distance(state.tiles.begin(), it));
}

std::optional<int> movableTileIndex(const State &state, Direction direction) {
  const auto empty = emptyIndex(state);
  if (!empty.has_value()) {
    return std::nullopt;
  }
  const int grid = state.grid;
  const int row = *empty / grid;
  const int col = *empty % grid;
  switch (direction) {
  case Direction::up:
    if (row < grid - 1)
      return (row + 1) * grid + col;
    break;
  case Direction::down:
    if (row > 0)
      return (row - 1) * grid + col;
    break;
  case Direction::left:
    if (col < grid - 1)
      return row * grid + (col + 1);
    break;
  case Direction::right:
    if (col > 0)
      return row * grid + (col - 1);
    break;
  }
  return std::nullopt;
}

ComposableArchitecture::Feature<State, Action> body() {
  using FeatureStore = ComposableArchitecture::Store<State, Action>;

  return ComposableArchitecture::Update<State, Action>([](State &state, const Action &action,
                                                          FeatureStore &store) {
           Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
           Dependencies::Dependency<Dependencies::RandomNumberGeneratorKey> rng;

           // Interrupting an in-progress auto-solve cancels its background
           // task.
           const auto stopSolving = [&] {
             if (state.isSolving) {
               state.isSolving = false;
               state.pendingMoves.clear();
               store.cancel(std::string(kSolverCancelId));
             }
           };

           // Persist the (already-mutated) settings off the main thread, the
           // Sharing analog of a save effect. Captures the strategy + a copy of
           // the new value, so the background write never touches state.
           const auto persistSettings = [&] {
             store.addTask([strategy = state.settings.strategy(), value = state.settings.get()](
                               FeatureStore &, std::stop_token) { strategy.save(value); });
           };

           std::visit(
               [&](auto &&value) {
                 using Value = std::decay_t<decltype(value)>;

                 if constexpr (std::is_same_v<Value, AutoSolveButtonTapped>) {
                   if (state.isSolving) {
                     stopSolving();
                   } else if (state.startDate.has_value() && !state.isGameOver) {
                     state.isSolving = true;
                     // Run the (history-reversing) planner on a background
                     // task; it reports the solution back as an action.
                     // Cancellable by id.
                     store.addTask(
                         [history = state.moveHistory, grid = state.grid](FeatureStore &store,
                                                                          std::stop_token stop) {
                           Dependencies::Dependency<SolverClient::Key> solver;
                           auto plan = solver->plan(history, grid, std::move(stop));
                           if (plan.has_value()) {
                             store.send(SolverSucceeded{std::move(*plan)});
                           } else {
                             store.send(SolverFailed{plan.error()});
                           }
                         },
                         std::string(kSolverCancelId));
                   }
                 } else if constexpr (std::is_same_v<Value, BoardSizeSelected>) {
                   stopSolving();
                   startNewGame(state, value.grid, *rng);
                   state.startDate = date->now();
                   // Remember the chosen board size for next launch.
                   state.settings.withMutation(
                       [grid = value.grid](AppSettings::Settings &s) { s.lastBoardSize = grid; });
                   persistSettings();
                 } else if constexpr (std::is_same_v<Value, SolverSucceeded>) {
                   if (state.isSolving) {
                     state.pendingMoves = std::move(value.moves);
                     state.nextMoveAt = date->now();
                     const double count =
                         static_cast<double>(std::max<std::size_t>(1, state.pendingMoves.size()));
                     state.solveInterval = std::clamp(4.0 / count, 0.0008, 0.10);
                     if (state.pendingMoves.empty()) {
                       state.isSolving = false;
                     }
                   }
                 } else if constexpr (std::is_same_v<Value, SolverFailed>) {
                   state.isSolving = false;
                   state.pendingMoves.clear();
                 } else if constexpr (std::is_same_v<Value, NearWinShortcutActivated>) {
                   stopSolving();
                   state.tiles = solvedTiles(state.grid);
                   state.moveHistory.clear();
                   int empty = state.grid * state.grid - 1;
                   for (int step = 0; step < 2; ++step) {
                     auto options = neighbors(empty, state.grid);
                     const int pick = options[PuzzleCore::nextIndex(*rng, options.size())];
                     std::swap(state.tiles[empty], state.tiles[pick]);
                     state.moveHistory.push_back(pick);
                     empty = pick;
                   }
                 } else if constexpr (std::is_same_v<Value, RestartButtonTapped>) {
                   stopSolving();
                   startNewGame(state, state.grid, *rng);
                   state.startDate = date->now();
                 } else if constexpr (std::is_same_v<Value, ShuffleButtonTapped>) {
                   stopSolving();
                   scramble(state.grid, *rng, state.tiles, state.moveHistory,
                            state.grid * state.grid * 10);
                 } else if constexpr (std::is_same_v<Value, SoundToggleButtonTapped>) {
                   state.settings.withMutation(
                       [](AppSettings::Settings &s) { s.isSoundEnabled = !s.isSoundEnabled; });
                   persistSettings();
                 } else if constexpr (std::is_same_v<Value, TileTapped>) {
                   if (state.isSolving) {
                     stopSolving();
                   } else {
                     applySlide(state, value.index);
                   }
                 } else if constexpr (std::is_same_v<Value, TimerTicked>) {
                   if (state.startDate.has_value() && !state.isGameOver) {
                     const double now = date->now();
                     const int seconds = static_cast<int>(now - *state.startDate);
                     if (seconds > state.secondsElapsed) {
                       state.secondsElapsed = seconds;
                       if (state.settings.get().isSoundEnabled) {
                         store.addTask([](FeatureStore &, std::stop_token) {
                           Dependencies::Dependency<AudioPlayerClient::Key> audioPlayer;
                           audioPlayer->play(AudioPlayerClient::Sound::tick);
                         });
                       }
                     }
                     // Animate queued auto-solve moves (catch up multiple per
                     // frame).
                     int applied = 0;
                     while (state.isSolving && !state.pendingMoves.empty() &&
                            now >= state.nextMoveAt && applied < 64) {
                       applySlide(state, state.pendingMoves.front());
                       state.pendingMoves.erase(state.pendingMoves.begin());
                       state.nextMoveAt += state.solveInterval;
                       ++applied;
                     }
                     if (state.isSolving && state.pendingMoves.empty()) {
                       state.isSolving = false;
                     }
                   }
                 }
               },
               action);

           const bool solved = isSolved(state.tiles, state.grid);
           if (solved && !state.isGameOver) {
             if (state.startDate.has_value()) {
               state.lastDuration = static_cast<int>(date->now() - *state.startDate);
             }
             state.startDate = std::nullopt;
           }
           state.isGameOver = solved;
         })
      .onMount([](State &state, FeatureStore &) {
        // First run: shuffle and start the timer (replaces the old AppLaunched
        // action).
        if (!state.startDate.has_value()) {
          Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
          Dependencies::Dependency<Dependencies::RandomNumberGeneratorKey> rng;
          startNewGame(state, state.grid, *rng);
          state.startDate = date->now();
        }
      });
}

} // namespace PuzzleFeature
