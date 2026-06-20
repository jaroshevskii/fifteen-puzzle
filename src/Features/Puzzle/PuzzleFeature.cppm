export module PuzzleFeature;

import std;
import ComposableArchitecture;

// Interface unit: declarations only. The implementation lives in
// PuzzleFeature.cpp (a module implementation unit), so editing reducer logic
// recompiles just that object and relinks — it does not change this module's
// BMI, so importers (AppFeature, the view, tests) are not rebuilt.
export namespace PuzzleFeature {

namespace Config {
constexpr float cardSize = 94.0f;
constexpr int grid = 4;
constexpr int tileCount = grid * grid;
constexpr float uiHeight = 50.0f;
}  // namespace Config

struct State {
  bool isGameOver = false;
  bool isSoundEnabled = false;
  std::optional<int> lastDuration;
  int secondsElapsed = 0;
  std::optional<double> startDate;
  std::vector<std::string> tiles;

  bool operator==(const State&) const = default;
};

// Action cases are named after what the user does, or after the data an effect
// feeds back (e.g. `TimerStarted`, `TimerTicked`), mirroring TCA conventions.
struct AppLaunched {};
struct NearWinShortcutActivated {};
struct RestartButtonTapped {};
struct ShuffleButtonTapped {};
struct SoundToggleButtonTapped {};
struct TileTapped {
  int index = 0;
};
struct TimerStarted {
  double date = 0.0;
};
struct TimerTicked {};

using Action = std::variant<
    AppLaunched,
    NearWinShortcutActivated,
    RestartButtonTapped,
    ShuffleButtonTapped,
    SoundToggleButtonTapped,
    TileTapped,
    TimerStarted,
    TimerTicked>;

using Effect = ComposableArchitecture::Effect<Action>;

enum class Direction {
  up,
  down,
  left,
  right,
};

State initialState();
ComposableArchitecture::ReducerFunction<State, Action> body();
int displayedSeconds(const State& state);
std::optional<int> emptyIndex(const State& state);
std::optional<int> movableTileIndex(const State& state, Direction direction);

}  // namespace PuzzleFeature
