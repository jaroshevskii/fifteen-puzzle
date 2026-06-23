module;

#include <raylib.h>

export module AppFeatureView;

import std;
import AppFeature;
import PuzzleFeature;
import PuzzleFeatureView;
import LeaderboardFeature;
import LeaderboardFeatureView;
import SettingsFeature;
import SettingsFeatureView;
import MenuView;

export namespace AppFeatureView {

std::vector<AppFeature::Action> collectActions(const AppFeature::State &state);
void draw(const AppFeature::State &state);

} // namespace AppFeatureView

// --- Implementation
// -----------------------------------------------------------

namespace AppFeatureView {

namespace {

// Keyboard selection per menu-style screen (view-only state, like the
// double-press timer in PuzzleFeatureView).
int menuSelected = 0;
int pausedSelected = 0;
int gameOverSelected = 0;

std::vector<MenuView::Button> mainMenuButtons(const AppFeature::State &state) {
  return {MenuView::Button{"New Game"},
          MenuView::Button{"Continue", AppFeature::hasResumableGame(state)},
          MenuView::Button{"Settings"}, MenuView::Button{"Leaderboard"}, MenuView::Button{"Quit"}};
}

const std::vector<MenuView::Button> kPausedButtons{
    {"Resume"}, {"Restart"}, {"Settings"}, {"Main Menu"}};
const std::vector<MenuView::Button> kGameOverButtons{{"Play Again"}, {"Main Menu"}};

// Runs keyboard/mouse selection over a button list, appending the chosen mapped
// action (if any) to `out`.
void runMenu(const std::vector<MenuView::Button> &buttons,
             const std::vector<AppFeature::Action> &mapped, int &selected,
             std::vector<AppFeature::Action> &out) {
  MenuView::moveSelection(buttons, selected);
  std::optional<int> chosen;
  if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) && selected >= 0 &&
      selected < static_cast<int>(buttons.size()) && buttons[selected].enabled) {
    chosen = selected;
  }
  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
    if (const int i = MenuView::buttonAtPoint(buttons, GetMousePosition()); i >= 0) {
      chosen = i;
    }
  }
  if (chosen) {
    out.push_back(mapped[*chosen]);
  }
}

std::string victorySubtitle(const AppFeature::GameOverScreen &screen) {
  return std::format("{:02}:{:02}   {} moves", screen.durationSeconds / 60,
                     screen.durationSeconds % 60, screen.moves);
}

void dim() { DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 180}); }

} // namespace

std::vector<AppFeature::Action> collectActions(const AppFeature::State &state) {
  std::vector<AppFeature::Action> actions;

  if (!state.destination.has_value()) {
    // In game: the puzzle owns input (and emits TimerTicked, so the clock only
    // advances here — pause/menus freeze it).
    for (const auto &puzzleAction : PuzzleFeatureView::collectActions(state.puzzle)) {
      actions.push_back(AppFeature::Puzzle{puzzleAction});
    }
    if (IsKeyPressed(KEY_M)) {
      actions.push_back(AppFeature::Puzzle{PuzzleFeature::SoundToggleButtonTapped{}});
    }
    if (IsKeyPressed(KEY_L)) {
      actions.push_back(AppFeature::OpenLeaderboard{});
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
      actions.push_back(AppFeature::PauseTapped{});
    }
    return actions;
  }

  std::visit(
      [&](auto &&screen) {
        using S = std::decay_t<decltype(screen)>;
        if constexpr (std::is_same_v<S, AppFeature::MainMenuScreen>) {
          runMenu(mainMenuButtons(state),
                  {AppFeature::StartNewGame{}, AppFeature::ContinueGame{},
                   AppFeature::OpenSettings{}, AppFeature::OpenLeaderboard{},
                   AppFeature::QuitTapped{}},
                  menuSelected, actions);
        } else if constexpr (std::is_same_v<S, AppFeature::PausedScreen>) {
          runMenu(kPausedButtons,
                  {AppFeature::Resume{}, AppFeature::Puzzle{PuzzleFeature::RestartButtonTapped{}},
                   AppFeature::OpenSettings{}, AppFeature::ShowMenu{}},
                  pausedSelected, actions);
          if (IsKeyPressed(KEY_ESCAPE)) {
            actions.push_back(AppFeature::Resume{});
          }
        } else if constexpr (std::is_same_v<S, AppFeature::GameOverScreen>) {
          runMenu(kGameOverButtons, {AppFeature::PlayAgain{}, AppFeature::ShowMenu{}},
                  gameOverSelected, actions);
          if (IsKeyPressed(KEY_ESCAPE)) {
            actions.push_back(AppFeature::ShowMenu{});
          }
        } else if constexpr (std::is_same_v<S, SettingsFeature::State>) {
          for (const auto &settingsAction : SettingsFeatureView::collectActions(screen)) {
            actions.push_back(AppFeature::Settings{settingsAction});
          }
          if (IsKeyPressed(KEY_ESCAPE)) {
            actions.push_back(AppFeature::Dismiss{});
          }
        } else if constexpr (std::is_same_v<S, LeaderboardFeature::State>) {
          if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_L)) {
            actions.push_back(AppFeature::Dismiss{});
          }
        }
      },
      *state.destination);

  return actions;
}

void draw(const AppFeature::State &state) {
  if (!state.destination.has_value()) {
    PuzzleFeatureView::draw(state.puzzle);
    return;
  }

  std::visit(
      [&](auto &&screen) {
        using S = std::decay_t<decltype(screen)>;
        if constexpr (std::is_same_v<S, AppFeature::MainMenuScreen>) {
          MenuView::draw("15 Puzzle", "", mainMenuButtons(state), menuSelected);
        } else if constexpr (std::is_same_v<S, AppFeature::PausedScreen>) {
          PuzzleFeatureView::draw(state.puzzle);
          dim();
          MenuView::draw("Paused", "", kPausedButtons, pausedSelected);
        } else if constexpr (std::is_same_v<S, AppFeature::GameOverScreen>) {
          PuzzleFeatureView::draw(state.puzzle);
          dim();
          MenuView::draw("Victory!", victorySubtitle(screen), kGameOverButtons, gameOverSelected);
        } else if constexpr (std::is_same_v<S, SettingsFeature::State>) {
          SettingsFeatureView::draw(screen);
        } else if constexpr (std::is_same_v<S, LeaderboardFeature::State>) {
          LeaderboardFeatureView::draw(screen);
        }
      },
      *state.destination);
}

} // namespace AppFeatureView
