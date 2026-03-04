#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace PuzzleFeature {

namespace Config {
constexpr float cardSize = 94.0f;
constexpr int grid = 4;
constexpr int tileCount = grid * grid;
constexpr float uiHeight = 50.0f;
}  // namespace Config

struct State {
  bool isEnd = false;
  std::optional<int> lastDuration;
  std::optional<double> startTime;
  std::vector<std::string> tiles;
};

struct AppLaunched {};
struct NearWinShortcutActivated {};
struct RestartButtonTapped {};
struct ShuffleButtonTapped {};
struct TileTapped {
  int index;
};
struct TimerStarted {
  double time;
};

using Action = std::variant<
    AppLaunched,
    NearWinShortcutActivated,
    RestartButtonTapped,
    ShuffleButtonTapped,
    TileTapped,
    TimerStarted>;

struct StartTimerRequested {};

using Effect = std::variant<StartTimerRequested>;
using Dispatch = std::function<void(const Action&)>;

enum class Direction {
  up,
  down,
  left,
  right,
};

State makeInitialState();
int elapsedSeconds(const State& state, double nowSeconds);
std::optional<int> emptyIndex(const State& state);
std::optional<int> movableTileIndex(const State& state, Direction direction);
std::pair<State, std::vector<Effect>> reduce(const State& state, const Action& action, double nowSeconds);

}  // namespace PuzzleFeature
