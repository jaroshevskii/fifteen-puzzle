module AppFeature; // implementation unit

import std;
import ComposableArchitecture;
import Dependencies;
import PuzzleFeature;
import LeaderboardFeature;
import MultiplayerFeature;
import LiveFeature;
import SettingsFeature;
import SavedGame;
import SharedModels;
import DatabaseClient;
import ApiClient;
import Sharing;
import AppSettings;

namespace AppFeature {

namespace {

SavedGame::Game snapshotOf(const PuzzleFeature::State &puzzle) {
  return SavedGame::Game{.grid = puzzle.grid,
                         .tiles = puzzle.tiles,
                         .moveHistory = puzzle.moveHistory,
                         .secondsElapsed = puzzle.secondsElapsed};
}

// Restores an in-progress game into the puzzle, back-dating `startDate` so the
// timer resumes from `secondsElapsed`. Leaving `startDate` set means
// PuzzleFeature::onMount won't reshuffle.
void restorePuzzle(PuzzleFeature::State &puzzle, const SavedGame::Game &saved, double now) {
  puzzle.grid = saved.grid;
  puzzle.tiles = saved.tiles;
  puzzle.moveHistory = saved.moveHistory;
  puzzle.secondsElapsed = saved.secondsElapsed;
  puzzle.isGameOver = false;
  puzzle.isSolving = false;
  puzzle.pendingMoves.clear();
  puzzle.lastDuration = std::nullopt;
  puzzle.startDate = now - static_cast<double>(saved.secondsElapsed);
}

} // namespace

State initialState(Sharing::Shared<AppSettings::Settings> settings,
                   Sharing::Shared<std::optional<SavedGame::Game>> savedGame) {
  State state{.puzzle = PuzzleFeature::initialState(std::move(settings)),
              .savedGame = std::move(savedGame)};

  if (const auto &saved = state.savedGame.get(); saved.has_value()) {
    Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
    restorePuzzle(state.puzzle, *saved, date->now());
    // Auto-resume jumps straight into the game; otherwise play the intro first
    // (it transitions to the menu, which then offers Continue).
    state.destination = state.puzzle.settings.get().autoResume
                            ? std::optional<Destination>{}
                            : std::optional<Destination>{IntroScreen{}};
  } else {
    state.destination = IntroScreen{};
  }
  return state;
}

bool hasResumableGame(const State &state) { return state.savedGame.get().has_value(); }

const AppSettings::Settings &effectiveSettings(const State &state) {
  if (state.destination.has_value()) {
    if (const auto *settings = std::get_if<SettingsFeature::State>(&*state.destination)) {
      return settings->settings.get();
    }
  }
  return state.puzzle.settings.get();
}

ComposableArchitecture::Feature<State, Action> body() {
  using FeatureStore = ComposableArchitecture::Store<State, Action>;

  auto feature =
      ComposableArchitecture::Scope<State, Action, PuzzleFeature::State, PuzzleFeature::Action>(
          &State::puzzle,
          ComposableArchitecture::casePath<Action, Puzzle, PuzzleFeature::Action>(&Puzzle::action),
          PuzzleFeature::body());

  feature.add(ComposableArchitecture::Update<State, Action>([](State &state, const Action &action,
                                                               FeatureStore &store) {
    // Persist (or clear) the saved game off the main thread.
    const auto saveGame = [&](std::optional<SavedGame::Game> value) {
      state.savedGame.set(value);
      store.addTask([strategy = state.savedGame.strategy(),
                     value](FeatureStore &, std::stop_token) { strategy.save(value); });
    };

    // --- navigation ---------------------------------------------------------
    std::visit(
        [&](auto &&value) {
          using V = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<V, ShowMenu>) {
            state.destination = MainMenuScreen{};
          } else if constexpr (std::is_same_v<V, StartNewGame> || std::is_same_v<V, PlayAgain>) {
            state.destination = std::nullopt;
            // The reshuffle is queued; the board is still solved this pass, but
            // the win trigger is edge-based (`onChange` below) so it will not
            // re-fire — it only reacts to isGameOver flipping true, which next
            // happens on a fresh solve.
            store.send(Puzzle{
                PuzzleFeature::BoardSizeSelected{state.puzzle.settings.get().lastBoardSize}});
          } else if constexpr (std::is_same_v<V, ContinueGame>) {
            if (const auto &saved = state.savedGame.get(); saved.has_value()) {
              Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
              restorePuzzle(state.puzzle, *saved, date->now());
            }
            state.destination = std::nullopt;
          } else if constexpr (std::is_same_v<V, PauseTapped>) {
            state.destination = PausedScreen{};
          } else if constexpr (std::is_same_v<V, Resume>) {
            state.destination = std::nullopt;
          } else if constexpr (std::is_same_v<V, OpenSettings>) {
            state.returnDestination = state.destination;
            state.destination = SettingsFeature::initialState(state.puzzle.settings);
          } else if constexpr (std::is_same_v<V, OpenLeaderboard>) {
            state.returnDestination = state.destination;
            LeaderboardFeature::State leaderboard;
            leaderboard.gridSize = state.puzzle.grid;
            leaderboard.isVisible = true;
            state.destination = std::move(leaderboard);
            store.send(Leaderboard{LeaderboardFeature::Appeared{}}); // explicit lifecycle
          } else if constexpr (std::is_same_v<V, OpenMultiplayer>) {
            state.returnDestination = state.destination;
            state.destination = MultiplayerFeature::initialState(
                state.puzzle.settings.get().playerName, state.puzzle.settings.get().lastBoardSize);
            store.send(Multiplayer{MultiplayerFeature::Appeared{}}); // explicit lifecycle
          } else if constexpr (std::is_same_v<V, OpenLive>) {
            state.returnDestination = state.destination;
            state.destination = LiveFeature::initialState();
            store.send(Live{LiveFeature::Appeared{}}); // explicit lifecycle
          } else if constexpr (std::is_same_v<V, Dismiss>) {
            if (state.destination.has_value()) {
              if (const auto *settings = std::get_if<SettingsFeature::State>(&*state.destination)) {
                state.puzzle.settings = settings->settings; // sync edits back
              }
              if (std::holds_alternative<MultiplayerFeature::State>(*state.destination)) {
                // Tear down the connection task; its stop_token sends a polite
                // Leave to the server on the way out.
                store.cancel(std::string(MultiplayerFeature::kConnectionCancelId));
              }
              if (std::holds_alternative<LiveFeature::State>(*state.destination)) {
                store.cancel(std::string(LiveFeature::kConnectionCancelId));
              }
            }
            state.destination = state.returnDestination.value_or(Destination{MainMenuScreen{}});
            state.returnDestination = std::nullopt;
          } else if constexpr (std::is_same_v<V, QuitTapped>) {
            state.wantsQuit = true;
          }
        },
        action);

    // --- autosave (throttled): only while actively playing ------------------
    if (!state.destination.has_value() && !state.puzzle.isGameOver &&
        state.puzzle.startDate.has_value()) {
      const auto signature =
          std::make_tuple(state.puzzle.grid, static_cast<int>(state.puzzle.moveHistory.size()),
                          state.puzzle.secondsElapsed);
      if (state.lastSavedSig != signature) {
        state.lastSavedSig = signature;
        saveGame(snapshotOf(state.puzzle));
      }
    }
  }));

  feature.add(ComposableArchitecture::ifCaseLet<State, Action, Destination, SettingsFeature::State,
                                                SettingsFeature::Action>(
      &State::destination, ComposableArchitecture::caseState<Destination, SettingsFeature::State>(),
      ComposableArchitecture::casePath<Action, Settings, SettingsFeature::Action>(
          &Settings::action),
      SettingsFeature::body()));

  feature.add(
      ComposableArchitecture::ifCaseLet<State, Action, Destination, LeaderboardFeature::State,
                                        LeaderboardFeature::Action>(
          &State::destination,
          ComposableArchitecture::caseState<Destination, LeaderboardFeature::State>(),
          ComposableArchitecture::casePath<Action, Leaderboard, LeaderboardFeature::Action>(
              &Leaderboard::action),
          LeaderboardFeature::body()));

  feature.add(
      ComposableArchitecture::ifCaseLet<State, Action, Destination, MultiplayerFeature::State,
                                        MultiplayerFeature::Action>(
          &State::destination,
          ComposableArchitecture::caseState<Destination, MultiplayerFeature::State>(),
          ComposableArchitecture::casePath<Action, Multiplayer, MultiplayerFeature::Action>(
              &Multiplayer::action),
          MultiplayerFeature::body()));

  feature.add(ComposableArchitecture::ifCaseLet<State, Action, Destination, LiveFeature::State,
                                                LiveFeature::Action>(
      &State::destination, ComposableArchitecture::caseState<Destination, LiveFeature::State>(),
      ComposableArchitecture::casePath<Action, Live, LiveFeature::Action>(&Live::action),
      LiveFeature::body()));

  // --- win trigger: fires the frame the puzzle flips to solved --------------
  // Declarative edge detection via the `onChange` trigger port, replacing the
  // old manual `didSubmitCurrentWin` flag. On the false→true transition it
  // submits the score (DB + API, off the main thread), clears the saved game,
  // and shows the victory screen. Appended last, so it observes the solved
  // state produced by the puzzle scope above.
  feature.onChange([](const State &state) { return state.puzzle.isGameOver; },
                   [](bool /*wasOver*/, bool isOver, State &state, FeatureStore &store) {
                     if (!isOver || !state.puzzle.lastDuration.has_value()) {
                       return; // solved without a timed run (nothing to record)
                     }
                     Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
                     const SharedModels::ScoreSubmission submission{
                         .name = state.puzzle.settings.get().playerName,
                         .gridSize = state.puzzle.grid,
                         .moves = static_cast<int>(state.puzzle.moveHistory.size()),
                         .duration = *state.puzzle.lastDuration,
                         .playedAt = date->now()};
                     store.addTask([submission](FeatureStore &, std::stop_token) {
                       Dependencies::Dependency<DatabaseClient::Key> db;
                       (void)db->saveGame(submission);
                     });
                     store.addTask([submission](FeatureStore &, std::stop_token stop) {
                       Dependencies::Dependency<ApiClient::Key> api;
                       (void)api->submitScore(submission, std::move(stop));
                     });
                     // Clear the saved game off the main thread (win ends the session).
                     state.savedGame.set(std::nullopt);
                     store.addTask(
                         [strategy = state.savedGame.strategy()](FeatureStore &, std::stop_token) {
                           strategy.save(std::nullopt);
                         });
                     state.lastSavedSig = std::nullopt;
                     state.destination =
                         GameOverScreen{.durationSeconds = *state.puzzle.lastDuration,
                                        .moves = static_cast<int>(state.puzzle.moveHistory.size())};
                   });

  return feature;
}

} // namespace AppFeature
