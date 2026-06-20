module PuzzleFeature;  // implementation unit

import std;
import ComposableArchitecture;
import AudioPlayerClient;
import SolverClient;

namespace PuzzleFeature {

namespace {

using Dependencies::RandomNumberGenerator;

constexpr std::string_view kSolverCancelId = "auto-solve";

int rowOf(int index, int grid) { return index / grid; }
int colOf(int index, int grid) { return index % grid; }

bool isAdjacent(int lhs, int rhs, int grid) {
  const int r1 = rowOf(lhs, grid), c1 = colOf(lhs, grid);
  const int r2 = rowOf(rhs, grid), c2 = colOf(rhs, grid);
  return (r1 == r2 && std::abs(c1 - c2) == 1) || (c1 == c2 && std::abs(r1 - r2) == 1);
}

std::vector<std::string> solvedTiles(int grid) {
  const int count = grid * grid;
  std::vector<std::string> tiles;
  tiles.reserve(count);
  for (int i = 1; i < count; ++i) {
    tiles.push_back(std::to_string(i));
  }
  tiles.emplace_back();  // empty cell last
  return tiles;
}

bool isSolved(const std::vector<std::string>& tiles, int grid) {
  const int count = grid * grid;
  for (int i = 0; i < count - 1; ++i) {
    if (tiles[i] != std::to_string(i + 1)) {
      return false;
    }
  }
  return tiles[count - 1].empty();
}

std::vector<int> neighbors(int pos, int grid) {
  const int r = rowOf(pos, grid), c = colOf(pos, grid);
  std::vector<int> result;
  if (r > 0) result.push_back(pos - grid);
  if (r < grid - 1) result.push_back(pos + grid);
  if (c > 0) result.push_back(pos - 1);
  if (c < grid - 1) result.push_back(pos + 1);
  return result;
}

// Builds a board by applying `count` random legal slides from solved, recording
// the move history (so the board is always solvable and reversible).
void scramble(int grid, RandomNumberGenerator& rng, std::vector<std::string>& tiles, std::vector<int>& history,
              int count) {
  tiles = solvedTiles(grid);
  history.clear();
  int empty = grid * grid - 1;
  int previous = -1;
  for (int i = 0; i < count; ++i) {
    auto options = neighbors(empty, grid);
    std::erase(options, previous);  // avoid immediately undoing the last slide
    const int pick = options[std::uniform_int_distribution<std::size_t>(0, options.size() - 1)(rng)];
    std::swap(tiles[empty], tiles[pick]);
    history.push_back(pick);
    previous = empty;
    empty = pick;
  }
  if (isSolved(tiles, grid) && !neighbors(empty, grid).empty()) {
    const int pick = neighbors(empty, grid).front();
    std::swap(tiles[empty], tiles[pick]);
    history.push_back(pick);
  }
}

void startNewGame(State& state, int grid, RandomNumberGenerator& rng) {
  state.grid = grid;
  state.isGameOver = false;
  state.lastDuration = std::nullopt;
  state.secondsElapsed = 0;
  state.startDate = std::nullopt;
  scramble(grid, rng, state.tiles, state.moveHistory, grid * grid * 10);
}

// Slides the tile at `pos` into the empty cell (if adjacent), recording the move.
bool applySlide(State& state, int pos) {
  const auto empty = emptyIndex(state);
  if (empty.has_value() && pos >= 0 && pos < static_cast<int>(state.tiles.size()) &&
      isAdjacent(pos, *empty, state.grid)) {
    std::swap(state.tiles[pos], state.tiles[*empty]);
    state.moveHistory.push_back(pos);
    return true;
  }
  return false;
}

}  // namespace

State initialState() {
  return State{.tiles = solvedTiles(Config::minGrid)};
}

int displayedSeconds(const State& state) {
  if (state.isGameOver && state.lastDuration.has_value()) {
    return *state.lastDuration;
  }
  return state.secondsElapsed;
}

std::optional<int> emptyIndex(const State& state) {
  const auto it = std::ranges::find(state.tiles, std::string{});
  if (it == state.tiles.end()) {
    return std::nullopt;
  }
  return static_cast<int>(std::ranges::distance(state.tiles.begin(), it));
}

std::optional<int> movableTileIndex(const State& state, Direction direction) {
  const auto empty = emptyIndex(state);
  if (!empty.has_value()) {
    return std::nullopt;
  }
  const int grid = state.grid;
  const int row = *empty / grid;
  const int col = *empty % grid;
  switch (direction) {
    case Direction::up:
      if (row < grid - 1) return (row + 1) * grid + col;
      break;
    case Direction::down:
      if (row > 0) return (row - 1) * grid + col;
      break;
    case Direction::left:
      if (col < grid - 1) return row * grid + (col + 1);
      break;
    case Direction::right:
      if (col > 0) return row * grid + (col - 1);
      break;
  }
  return std::nullopt;
}

ComposableArchitecture::ReducerFunction<State, Action> body() {
  return ComposableArchitecture::Reduce<State, Action>([](State& state, const Action& action) -> Effect {
    Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
    Dependencies::Dependency<Dependencies::RandomNumberGeneratorKey> rng;

    const auto startTimer = [] {
      return Effect::run([](const ComposableArchitecture::Send<Action>& send) {
        Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
        send(TimerStarted{date->now()});
      });
    };

    bool interruptSolve = false;
    const auto stopSolving = [&] {
      if (state.isSolving) {
        state.isSolving = false;
        state.pendingMoves.clear();
        interruptSolve = true;
      }
    };

    Effect effect = Effect::none();

    std::visit(
        [&](auto&& value) {
          using Value = std::decay_t<decltype(value)>;

          if constexpr (std::is_same_v<Value, AppLaunched>) {
            if (!state.startDate.has_value()) {
              startNewGame(state, state.grid, *rng);
              effect = startTimer();
            }
          } else if constexpr (std::is_same_v<Value, AutoSolveButtonTapped>) {
            if (state.isSolving) {
              stopSolving();
            } else if (state.startDate.has_value() && !state.isGameOver) {
              state.isSolving = true;
              effect = Effect::task([history = state.moveHistory, grid = state.grid](
                                        const ComposableArchitecture::Send<Action>& send, std::stop_token stop) {
                         Dependencies::Dependency<SolverClient::Key> solver;
                         auto plan = solver->plan(history, grid, std::move(stop));
                         if (plan.has_value()) {
                           send(SolverSucceeded{std::move(*plan)});
                         } else {
                           send(SolverFailed{plan.error()});
                         }
                       }).cancellable(std::string(kSolverCancelId));
            }
          } else if constexpr (std::is_same_v<Value, BoardSizeSelected>) {
            stopSolving();
            startNewGame(state, value.grid, *rng);
            effect = startTimer();
          } else if constexpr (std::is_same_v<Value, SolverSucceeded>) {
            if (state.isSolving) {
              state.pendingMoves = std::move(value.moves);
              state.nextMoveAt = date->now();
              const double count = static_cast<double>(std::max<std::size_t>(1, state.pendingMoves.size()));
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
              const int pick = options[std::uniform_int_distribution<std::size_t>(0, options.size() - 1)(*rng)];
              std::swap(state.tiles[empty], state.tiles[pick]);
              state.moveHistory.push_back(pick);
              empty = pick;
            }
          } else if constexpr (std::is_same_v<Value, RestartButtonTapped>) {
            stopSolving();
            startNewGame(state, state.grid, *rng);
            effect = startTimer();
          } else if constexpr (std::is_same_v<Value, ShuffleButtonTapped>) {
            stopSolving();
            scramble(state.grid, *rng, state.tiles, state.moveHistory, state.grid * state.grid * 10);
          } else if constexpr (std::is_same_v<Value, SoundToggleButtonTapped>) {
            state.isSoundEnabled = !state.isSoundEnabled;
          } else if constexpr (std::is_same_v<Value, TileTapped>) {
            if (state.isSolving) {
              stopSolving();
            } else {
              applySlide(state, value.index);
            }
          } else if constexpr (std::is_same_v<Value, TimerStarted>) {
            state.startDate = value.date;
            state.secondsElapsed = 0;
          } else if constexpr (std::is_same_v<Value, TimerTicked>) {
            if (state.startDate.has_value() && !state.isGameOver) {
              const double now = date->now();
              const int seconds = static_cast<int>(now - *state.startDate);
              if (seconds > state.secondsElapsed) {
                state.secondsElapsed = seconds;
                if (state.isSoundEnabled) {
                  effect = Effect::run([](const ComposableArchitecture::Send<Action>&) {
                    Dependencies::Dependency<AudioPlayerClient::Key> audioPlayer;
                    audioPlayer->play(AudioPlayerClient::Sound::tick);
                  });
                }
              }
              // Animate queued auto-solve moves (catch up multiple per frame).
              int applied = 0;
              while (state.isSolving && !state.pendingMoves.empty() && now >= state.nextMoveAt && applied < 64) {
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

    if (interruptSolve) {
      effect = Effect::merge({Effect::cancel(std::string(kSolverCancelId)), std::move(effect)});
    }

    const bool solved = isSolved(state.tiles, state.grid);
    if (solved && !state.isGameOver) {
      if (state.startDate.has_value()) {
        state.lastDuration = static_cast<int>(date->now() - *state.startDate);
      }
      state.startDate = std::nullopt;
    }
    state.isGameOver = solved;

    return effect;
  });
}

}  // namespace PuzzleFeature
