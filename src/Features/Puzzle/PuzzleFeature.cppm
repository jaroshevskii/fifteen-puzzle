export module PuzzleFeature;

import std;
import ComposableArchitecture;
import SolverClient;

// Interface unit: declarations only (implementation in PuzzleFeature.cpp).
export namespace PuzzleFeature {

// Layout + sizing. The board grows with the grid up to a capped pixel size
// (3x the base 4x4 board); beyond that the tiles shrink to fit. Sizes are pure
// functions of the grid so the view and the window setup agree.
namespace Config {
constexpr int minGrid = 4;  // level 0 — the classic 15-puzzle
constexpr int maxGrid = 13; // level 9
constexpr float baseTileSize = 94.0f;
constexpr float uiHeight = 50.0f;
constexpr float maxBoardPixels =
    3.0f * baseTileSize * minGrid; // window caps at 3x the base board

constexpr int gridForLevel(int level) {
  const int g = minGrid + level;
  return g < minGrid ? minGrid : (g > maxGrid ? maxGrid : g);
}
constexpr float tileSize(int grid) {
  const float fit = maxBoardPixels / static_cast<float>(grid);
  return fit < baseTileSize ? fit : baseTileSize;
}
constexpr float boardPixels(int grid) {
  return tileSize(grid) * static_cast<float>(grid);
}
constexpr int windowWidth(int grid) {
  return static_cast<int>(boardPixels(grid) + 0.5f);
}
constexpr int windowHeight(int grid) {
  return static_cast<int>(boardPixels(grid) + uiHeight + 0.5f);
}
constexpr int fontSize(int grid) {
  const int f = static_cast<int>(tileSize(grid) * 0.5f);
  return f < 10 ? 10 : f;
}
} // namespace Config

struct State {
  int grid = Config::minGrid;
  bool isGameOver = false;
  bool isSolving = false;
  bool isSoundEnabled = false;
  std::optional<int> lastDuration;
  double nextMoveAt = 0.0;
  double solveInterval = 0.05;
  std::vector<int> moveHistory;  // slides from solved that produced `tiles`
  std::vector<int> pendingMoves; // queued auto-solve moves to animate
  int secondsElapsed = 0;
  std::optional<double> startDate;
  std::vector<std::string> tiles;

  bool operator==(const State &) const = default;
};

struct AutoSolveButtonTapped {};
struct BoardSizeSelected {
  int grid = Config::minGrid;
};
struct NearWinShortcutActivated {};
struct RestartButtonTapped {};
struct ShuffleButtonTapped {};
struct SoundToggleButtonTapped {};
struct SolverSucceeded {
  std::vector<int> moves;
};
struct SolverFailed {
  SolverClient::SolveError error = SolverClient::SolveError::cancelled;
};
struct TileTapped {
  int index = 0;
};
struct TimerTicked {};

using Action =
    std::variant<AutoSolveButtonTapped, BoardSizeSelected,
                 NearWinShortcutActivated, RestartButtonTapped,
                 ShuffleButtonTapped, SoundToggleButtonTapped, SolverSucceeded,
                 SolverFailed, TileTapped, TimerTicked>;

enum class Direction : std::uint8_t {
  up,
  down,
  left,
  right,
};

State initialState();
// The feature body (TCA 2.0 style). The first shuffle + timer start happen in
// onMount, so there is no AppLaunched action to send.
ComposableArchitecture::Feature<State, Action> body();
int displayedSeconds(const State &state);
std::optional<int> emptyIndex(const State &state);
std::optional<int> movableTileIndex(const State &state, Direction direction);

} // namespace PuzzleFeature
