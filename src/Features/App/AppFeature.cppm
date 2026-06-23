export module AppFeature;

import std;
import ComposableArchitecture;
import PuzzleFeature;
import LeaderboardFeature;
import SettingsFeature;
import SavedGame;
import Sharing;
import AppSettings;

// The root feature. The game (puzzle) state is always present, so it is never
// lost while visiting other screens; the non-game screens are a presented
// `std::optional<Destination>` (the TCA enum-`Destination` pattern). When
// `destination == nullopt` the game is shown; otherwise exactly one screen is.
export namespace AppFeature {

// Lightweight screens with no sub-feature of their own.
struct IntroScreen {
  bool operator==(const IntroScreen &) const = default;
};
struct MainMenuScreen {
  bool operator==(const MainMenuScreen &) const = default;
};
struct PausedScreen {
  bool operator==(const PausedScreen &) const = default;
};
struct GameOverScreen {
  int durationSeconds = 0;
  int moves = 0;
  bool operator==(const GameOverScreen &) const = default;
};

// Settings and Leaderboard carry real sub-feature state, scoped via `ifCaseLet`.
using Destination = std::variant<IntroScreen, MainMenuScreen, PausedScreen, GameOverScreen,
                                 SettingsFeature::State, LeaderboardFeature::State>;

struct State {
  PuzzleFeature::State puzzle;                  // always present
  std::optional<Destination> destination;       // nullopt => game shown
  std::optional<Destination> returnDestination; // where to go on Dismiss
  Sharing::Shared<std::optional<SavedGame::Game>> savedGame;
  bool didSubmitCurrentWin = false;                      // win edge-detect
  std::optional<std::tuple<int, int, int>> lastSavedSig; // autosave throttle
  bool wantsQuit = false;                                // main loop exits

  bool operator==(const State &) const = default;
};

// Child-action wrappers.
struct Puzzle {
  PuzzleFeature::Action action;
};
struct Leaderboard {
  LeaderboardFeature::Action action;
};
struct Settings {
  SettingsFeature::Action action;
};

// Navigation actions.
struct ShowMenu {};
struct StartNewGame {};
struct ContinueGame {};
struct PauseTapped {};
struct Resume {};
struct OpenSettings {};
struct OpenLeaderboard {};
struct Dismiss {};
struct PlayAgain {};
struct QuitTapped {};

using Action =
    std::variant<Puzzle, Leaderboard, Settings, ShowMenu, StartNewGame, ContinueGame, PauseTapped,
                 Resume, OpenSettings, OpenLeaderboard, Dismiss, PlayAgain, QuitTapped>;

// Builds the initial state. If a saved game exists it is restored into the
// puzzle; the launch screen is the game itself when `settings.autoResume` is on,
// otherwise the main menu (which then offers Continue).
State initialState(Sharing::Shared<AppSettings::Settings> settings = {},
                   Sharing::Shared<std::optional<SavedGame::Game>> savedGame = {});
ComposableArchitecture::Feature<State, Action> body();

// Whether a resumable, unfinished game exists (drives the menu's Continue item).
bool hasResumableGame(const State &state);

} // namespace AppFeature
