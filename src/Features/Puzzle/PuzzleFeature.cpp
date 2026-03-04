#include "Features/Puzzle/PuzzleFeature.hpp"

#include <algorithm>
#include <random>

namespace PuzzleFeature {

namespace {

std::mt19937& rng() {
  static std::mt19937 generator{std::random_device{}()};
  return generator;
}

int indexRow(int index) {
  return index / Config::grid;
}

int indexCol(int index) {
  return index % Config::grid;
}

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
  for (size_t index = 0; index < values.size(); ++index) {
    for (size_t nested = index + 1; nested < values.size(); ++nested) {
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

void shuffleLabels(std::vector<std::string>& labels) {
  std::shuffle(labels.begin(), labels.end(), rng());
}

std::vector<std::string> shuffledSolvableTiles() {
  std::vector<std::string> labels = solvedTiles();
  do {
    shuffleLabels(labels);
  } while (!isSolvable(labels) || isSolved(labels));
  return labels;
}

std::vector<std::string> nearWinTiles() {
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
    std::swap(tiles[candidates[distribution(rng())]], tiles[*empty]);
  }
  return tiles;
}

}  // namespace

State makeInitialState() {
  return State{
      .isEnd = false,
      .lastDuration = std::nullopt,
      .startTime = std::nullopt,
      .tiles = shuffledSolvableTiles()};
}

int elapsedSeconds(const State& state, double nowSeconds) {
  if (state.isEnd && state.lastDuration.has_value()) {
    return *state.lastDuration;
  }
  if (state.startTime.has_value()) {
    return static_cast<int>(nowSeconds - *state.startTime);
  }
  return 0;
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

std::pair<State, std::vector<Effect>> reduce(const State& state, const Action& action, double nowSeconds) {
  State nextState = state;
  std::vector<Effect> effects;

  std::visit(
      [&](auto&& value) {
        using Value = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<Value, AppLaunched>) {
          if (!state.startTime.has_value()) {
            effects.push_back(StartTimerRequested{});
          }
        } else if constexpr (std::is_same_v<Value, NearWinShortcutActivated>) {
          nextState.tiles = nearWinTiles();
        } else if constexpr (std::is_same_v<Value, RestartButtonTapped>) {
          nextState.isEnd = false;
          nextState.lastDuration = std::nullopt;
          nextState.startTime = std::nullopt;
          nextState.tiles = shuffledSolvableTiles();
          effects.push_back(StartTimerRequested{});
        } else if constexpr (std::is_same_v<Value, ShuffleButtonTapped>) {
          nextState.tiles = shuffledSolvableTiles();
        } else if constexpr (std::is_same_v<Value, TileTapped>) {
          const auto empty = emptyIndex(nextState);
          if (empty.has_value() && value.index >= 0 && value.index < static_cast<int>(nextState.tiles.size()) &&
              isAdjacent(value.index, *empty)) {
            std::swap(nextState.tiles[value.index], nextState.tiles[*empty]);
          }
        } else if constexpr (std::is_same_v<Value, TimerStarted>) {
          nextState.startTime = value.time;
        }
      },
      action);

  const bool solved = isSolved(nextState.tiles);
  if (solved && !nextState.isEnd) {
    if (nextState.startTime.has_value()) {
      nextState.lastDuration = static_cast<int>(nowSeconds - *nextState.startTime);
    }
    nextState.startTime = std::nullopt;
  }
  nextState.isEnd = solved;

  return {nextState, effects};
}

}  // namespace PuzzleFeature
