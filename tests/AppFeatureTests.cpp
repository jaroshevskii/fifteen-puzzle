// Tests AppFeature's navigation + state restoration with the TestStore. Time is
// pinned via the DateGenerator so transitions are deterministic. The in-game
// autosave fires on every in-game action, so tests pre-seed `lastSavedSig` (or
// account for it) to keep state assertions exact.

import std;
import ComposableArchitecture;
import AppFeature;
import PuzzleFeature;
import SavedGame;
import Sharing;
import AppSettings;

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
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

template <typename T> bool isScreen(const AppFeature::State &s) {
  return s.destination.has_value() && std::holds_alternative<T>(*s.destination);
}

// An in-game state with an almost-solved 4x4 board (empty at 14, "15" at 15),
// timer started at 0, and the autosave signature pre-seeded so navigation
// actions don't trigger a save.
AppFeature::State inGameState() {
  auto s = AppFeature::initialState();
  std::swap(s.puzzle.tiles[14], s.puzzle.tiles[15]);
  s.puzzle.startDate = 0.0;
  s.destination = std::nullopt;
  s.lastSavedSig = std::make_tuple(s.puzzle.grid, static_cast<int>(s.puzzle.moveHistory.size()),
                                   s.puzzle.secondsElapsed);
  return s;
}

void testLaunchShowsMenu() {
  withDependencies(
      [](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<DateGeneratorKey>(DateGenerator::constant(0.0));
        values.set<RandomNumberGeneratorKey>(RandomNumberGenerator::seeded(1));
      },
      [] {
        TestStore<AppFeature::State, AppFeature::Action> store(AppFeature::initialState(),
                                                               AppFeature::body);
        expect(isScreen<AppFeature::MainMenuScreen>(store.state()),
               "launch (no save) shows the main menu");
        expect(!AppFeature::hasResumableGame(store.state()), "no resumable game at first launch");
        return 0;
      });
}

void testPauseAndResume() {
  withDependencies(
      [](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<DateGeneratorKey>(DateGenerator::constant(0.0));
      },
      [] {
        TestStore<AppFeature::State, AppFeature::Action> store(inGameState(), AppFeature::body);
        store.send(AppFeature::PauseTapped{},
                   [](AppFeature::State &s) { s.destination = AppFeature::PausedScreen{}; });
        store.send(AppFeature::Resume{},
                   [](AppFeature::State &s) { s.destination = std::nullopt; });
        expect(!store.failed(), "pause then resume toggles the destination");
        return 0;
      });
}

void testWinClearsSavedGameAndShowsVictory() {
  withDependencies(
      [](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<DateGeneratorKey>(DateGenerator::constant(0.0));
      },
      [] {
        auto state = inGameState();
        // A saved game exists; winning must clear it.
        state.savedGame = Sharing::Shared<std::optional<SavedGame::Game>>(
            Sharing::inMemory<std::optional<SavedGame::Game>>(SavedGame::Game{
                .grid = 4, .tiles = {"x"}, .moveHistory = {1}, .secondsElapsed = 3}));

        TestStore<AppFeature::State, AppFeature::Action> store(std::move(state), AppFeature::body);

        store.send(AppFeature::Puzzle{PuzzleFeature::TileTapped{15}}, [](AppFeature::State &s) {
          std::swap(s.puzzle.tiles[14], s.puzzle.tiles[15]);
          s.puzzle.moveHistory.push_back(15);
          s.puzzle.isGameOver = true;
          s.puzzle.lastDuration = 0;
          s.puzzle.startDate = std::nullopt;
          s.didSubmitCurrentWin = true;
          s.savedGame.set(std::nullopt);
          s.lastSavedSig = std::nullopt;
          s.destination = AppFeature::GameOverScreen{.durationSeconds = 0, .moves = 1};
        });

        expect(!store.failed(), "winning shows Game Over and clears the save");
        expect(isScreen<AppFeature::GameOverScreen>(store.state()), "destination is GameOver");
        expect(!AppFeature::hasResumableGame(store.state()), "saved game cleared on win");
        return 0;
      });
}

void testContinueRestoresSavedGame() {
  withDependencies(
      [](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<DateGeneratorKey>(DateGenerator::constant(0.0));
      },
      [] {
        const SavedGame::Game saved{
            .grid = 5, .tiles = {"a", "b"}, .moveHistory = {2, 3}, .secondsElapsed = 7};
        auto state = AppFeature::initialState();
        state.puzzle.startDate = 0.0; // skip onMount reshuffle
        state.destination = AppFeature::MainMenuScreen{};
        state.savedGame = Sharing::Shared<std::optional<SavedGame::Game>>(
            Sharing::inMemory<std::optional<SavedGame::Game>>(saved));

        TestStore<AppFeature::State, AppFeature::Action> store(std::move(state), AppFeature::body);

        store.send(AppFeature::ContinueGame{}, [&](AppFeature::State &s) {
          s.puzzle.grid = 5;
          s.puzzle.tiles = saved.tiles;
          s.puzzle.moveHistory = saved.moveHistory;
          s.puzzle.secondsElapsed = 7;
          s.puzzle.startDate = -7.0; // back-dated from now(0) - secondsElapsed
          s.destination = std::nullopt;
          s.lastSavedSig = std::make_tuple(5, 2, 7); // autosave fires on entering the game
        });

        expect(!store.failed(), "Continue restores the saved board + elapsed time");
        return 0;
      });
}

} // namespace

int main() {
  testLaunchShowsMenu();
  testPauseAndResume();
  testWinClearsSavedGameAndShowsVictory();
  testContinueRestoresSavedGame();
  if (failures == 0) {
    std::println("All AppFeature tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} AppFeature test(s) failed.", failures);
  return 1;
}
