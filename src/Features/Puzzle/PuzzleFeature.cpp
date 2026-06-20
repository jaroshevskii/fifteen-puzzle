module PuzzleFeature;  // implementation unit for the PuzzleFeature module

import std;
import ComposableArchitecture;
import AudioPlayerClient;

namespace PuzzleFeature {

namespace {

using Dependencies::RandomNumberGenerator;

int indexRow(int index) { return index / Config::grid; }
int indexCol(int index) { return index % Config::grid; }

bool isAdjacent(int lhs, int rhs) {
  const int row1 = indexRow(lhs);
  const int col1 = indexCol(lhs);
  const int row2 = indexRow(rhs);
  const int col2 = indexCol(rhs);
  return (row1 == row2 && std::abs(col1 - col2) == 1) || (col1 == col2 && std::abs(row1 - row2) == 1);
}

std::vector<std::string> solvedTiles() {
  std::vector<std::string> tiles;
  tiles.reserve(Config::tileCount);

  for (int row = 0; row < Config::grid; ++row) {
    for (int col = 0; col < Config::grid; ++col) {
      const bool isEmpty = row == Config::grid - 1 && col == Config::grid - 1;
      tiles.emplace_back(isEmpty ? std::string{} : std::to_string(row * Config::grid + col + 1));
    }
  }
  return tiles;
}

bool isSolved(const std::vector<std::string>& tiles) {
  for (int index = 0; index < Config::tileCount - 1; ++index) {
    if (tiles[index] != std::to_string(index + 1)) {
      return false;
    }
  }
  return tiles[Config::tileCount - 1].empty();
}

int inversionCount(const std::vector<int>& values) {
  int inversions = 0;
  for (std::size_t index = 0; index < values.size(); ++index) {
    for (std::size_t nested = index + 1; nested < values.size(); ++nested) {
      if (values[index] > values[nested]) {
        ++inversions;
      }
    }
  }
  return inversions;
}

bool isSolvable(const std::vector<std::string>& labels) {
  std::vector<int> values;
  values.reserve(Config::tileCount - 1);

  int emptyRowFromBottom = 0;
  for (int index = 0; index < Config::tileCount; ++index) {
    if (labels[index].empty()) {
      emptyRowFromBottom = Config::grid - indexRow(index);
    } else {
      values.push_back(std::stoi(labels[index]));
    }
  }

  const int inversions = inversionCount(values);
  if (Config::grid % 2 == 1) {
    return inversions % 2 == 0;
  }
  if (emptyRowFromBottom % 2 == 0) {
    return inversions % 2 == 1;
  }
  return inversions % 2 == 0;
}

std::vector<std::string> shuffledSolvableTiles(RandomNumberGenerator& rng) {
  std::vector<std::string> labels = solvedTiles();
  do {
    std::shuffle(labels.begin(), labels.end(), rng);
  } while (!isSolvable(labels) || isSolved(labels));
  return labels;
}

std::vector<std::string> nearWinTiles(RandomNumberGenerator& rng) {
  auto tiles = solvedTiles();
  const auto empty = emptyIndex(State{.tiles = tiles});

  if (!empty.has_value()) {
    return tiles;
  }

  std::vector<int> candidates;
  for (int index = 0; index < static_cast<int>(tiles.size()); ++index) {
    if (isAdjacent(index, *empty)) {
      candidates.push_back(index);
    }
  }

  if (!candidates.empty()) {
    std::uniform_int_distribution<int> distribution(0, static_cast<int>(candidates.size()) - 1);
    std::swap(tiles[candidates[distribution(rng)]], tiles[*empty]);
  }
  return tiles;
}

}  // namespace

State initialState() {
  return State{.tiles = solvedTiles()};
}

int displayedSeconds(const State& state) {
  if (state.isGameOver && state.lastDuration.has_value()) {
    return *state.lastDuration;
  }
  return state.secondsElapsed;
}

std::optional<int> emptyIndex(const State& state) {
  const auto it = std::find_if(state.tiles.begin(), state.tiles.end(), [](const std::string& tile) { return tile.empty(); });
  if (it == state.tiles.end()) {
    return std::nullopt;
  }
  return static_cast<int>(std::distance(state.tiles.begin(), it));
}

std::optional<int> movableTileIndex(const State& state, Direction direction) {
  const auto empty = emptyIndex(state);
  if (!empty.has_value()) {
    return std::nullopt;
  }

  const int row = *empty / Config::grid;
  const int col = *empty % Config::grid;

  switch (direction) {
    case Direction::up:
      if (row < Config::grid - 1) {
        return (row + 1) * Config::grid + col;
      }
      break;
    case Direction::down:
      if (row > 0) {
        return (row - 1) * Config::grid + col;
      }
      break;
    case Direction::left:
      if (col < Config::grid - 1) {
        return row * Config::grid + (col + 1);
      }
      break;
    case Direction::right:
      if (col > 0) {
        return row * Config::grid + (col - 1);
      }
      break;
  }
  return std::nullopt;
}

ComposableArchitecture::ReducerFunction<State, Action> body() {
  return ComposableArchitecture::Reduce<State, Action>([](State& state, const Action& action) -> Effect {
    Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
    Dependencies::Dependency<Dependencies::RandomNumberGeneratorKey> rng;

    // Reads the clock and feeds the start instant back as `TimerStarted`,
    // exactly like a TCA `.run` effect that depends on `\.date`.
    const auto startTimer = [] {
      return Effect::run([](const ComposableArchitecture::Send<Action>& send) {
        Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
        send(TimerStarted{date->now()});
      });
    };

    Effect effect = Effect::none();

    std::visit(
        [&](auto&& value) {
          using Value = std::decay_t<decltype(value)>;

          if constexpr (std::is_same_v<Value, AppLaunched>) {
            if (!state.startDate.has_value()) {
              state.tiles = shuffledSolvableTiles(*rng);
              effect = startTimer();
            }
          } else if constexpr (std::is_same_v<Value, NearWinShortcutActivated>) {
            state.tiles = nearWinTiles(*rng);
          } else if constexpr (std::is_same_v<Value, RestartButtonTapped>) {
            state.isGameOver = false;
            state.lastDuration = std::nullopt;
            state.secondsElapsed = 0;
            state.startDate = std::nullopt;
            state.tiles = shuffledSolvableTiles(*rng);
            effect = startTimer();
          } else if constexpr (std::is_same_v<Value, ShuffleButtonTapped>) {
            state.tiles = shuffledSolvableTiles(*rng);
          } else if constexpr (std::is_same_v<Value, SoundToggleButtonTapped>) {
            state.isSoundEnabled = !state.isSoundEnabled;
          } else if constexpr (std::is_same_v<Value, TileTapped>) {
            const auto empty = emptyIndex(state);
            if (empty.has_value() && value.index >= 0 && value.index < static_cast<int>(state.tiles.size()) &&
                isAdjacent(value.index, *empty)) {
              std::swap(state.tiles[value.index], state.tiles[*empty]);
            }
          } else if constexpr (std::is_same_v<Value, TimerStarted>) {
            state.startDate = value.date;
            state.secondsElapsed = 0;
          } else if constexpr (std::is_same_v<Value, TimerTicked>) {
            if (state.startDate.has_value() && !state.isGameOver) {
              const int seconds = static_cast<int>(date->now() - *state.startDate);
              if (seconds > state.secondsElapsed) {
                state.secondsElapsed = seconds;
                if (state.isSoundEnabled) {
                  effect = Effect::run([](const ComposableArchitecture::Send<Action>&) {
                    Dependencies::Dependency<AudioPlayerClient::Key> audioPlayer;
                    audioPlayer->play(AudioPlayerClient::Sound::tick);
                  });
                }
              }
            }
          }
        },
        action);

    const bool solved = isSolved(state.tiles);
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
