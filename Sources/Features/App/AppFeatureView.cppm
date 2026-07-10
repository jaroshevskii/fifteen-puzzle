module;

#include <raylib.h>

export module AppFeatureView;

import std;
import AppFeature;
import PuzzleFeature;
import PuzzleFeatureView;
import LeaderboardFeature;
import LeaderboardFeatureView;
import MultiplayerFeature;
import MultiplayerFeatureView;
import LiveFeature;
import LiveFeatureView;
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
          MenuView::Button{"Multiplayer"},
          MenuView::Button{"Live Games"},
          MenuView::Button{"Settings"},
          MenuView::Button{"Leaderboard"},
          MenuView::Button{"Quit"}};
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

// --- intro animation ------------------------------------------------------
// View-driven (raylib clock), so it never touches feature state. The tiles
// 1..15 drop into a 4x4 board with a staggered ease-out, the title fades in,
// then the whole thing fades to the menu. Skippable with any key / click.
constexpr double kIntroDuration = 2.4;

double introElapsed() {
  static double start = -1.0;
  const double now = GetTime();
  if (start < 0.0) {
    start = now;
  }
  return now - start;
}

float easeOutCubic(float t) {
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}

bool introShouldFinish(double elapsed) {
  return elapsed >= kIntroDuration || GetKeyPressed() != 0 ||
         IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

void drawIntro(double elapsed) {
  const float w = static_cast<float>(GetScreenWidth());
  const float h = static_cast<float>(GetScreenHeight());
  const float tile = std::min(w, h) / 6.0f;
  const float board = tile * 4.0f;
  const float originX = (w - board) / 2.0f;
  const float originY = (h - board) / 2.0f + 10.0f;
  constexpr float animDur = 0.45f;
  constexpr float stagger = 0.05f;

  for (int i = 0; i < 15; ++i) { // 1..15; the 16th cell is empty
    const float progress =
        std::clamp(static_cast<float>((elapsed - i * stagger) / animDur), 0.0f, 1.0f);
    if (progress <= 0.0f) {
      continue;
    }
    const float eased = easeOutCubic(progress);
    const float targetY = originY + static_cast<float>(i / 4) * tile;
    const float y = -tile + (targetY + tile) * eased;
    const float x = originX + static_cast<float>(i % 4) * tile;
    const auto alpha = static_cast<unsigned char>(255 * std::clamp(progress * 1.5f, 0.0f, 1.0f));

    const Rectangle rect{x + 2.0f, y + 2.0f, tile - 4.0f, tile - 4.0f};
    DrawRectangleRec(rect, Color{230, 126, 34, alpha});
    const std::string label = std::to_string(i + 1);
    const int font = static_cast<int>(tile * 0.5f);
    DrawText(label.c_str(),
             static_cast<int>(rect.x + (rect.width - MeasureText(label.c_str(), font)) / 2.0f),
             static_cast<int>(rect.y + (rect.height - font) / 2.0f), font,
             Color{20, 20, 20, alpha});
  }

  const auto titleAlpha = static_cast<unsigned char>(
      255 * std::clamp(static_cast<float>((elapsed - 0.9) / 0.5), 0.0f, 1.0f));
  constexpr int titleFont = 36;
  DrawText("15 Puzzle", (GetScreenWidth() - MeasureText("15 Puzzle", titleFont)) / 2, 16, titleFont,
           Color{255, 255, 255, titleAlpha});

  if (elapsed > 1.4) {
    constexpr int hintFont = 16;
    DrawText("press any key", (GetScreenWidth() - MeasureText("press any key", hintFont)) / 2,
             GetScreenHeight() - 30, hintFont, Color{170, 170, 170, 200});
  }

  // Fade to black over the tail, so the cut to the menu is smooth.
  if (const double fadeStart = kIntroDuration - 0.3; elapsed > fadeStart) {
    const auto fade = static_cast<unsigned char>(
        255 * std::clamp(static_cast<float>((elapsed - fadeStart) / 0.3), 0.0f, 1.0f));
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, fade});
  }
}

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
        if constexpr (std::is_same_v<S, AppFeature::IntroScreen>) {
          if (introShouldFinish(introElapsed())) {
            actions.push_back(AppFeature::ShowMenu{});
          }
        } else if constexpr (std::is_same_v<S, AppFeature::MainMenuScreen>) {
          runMenu(mainMenuButtons(state),
                  {AppFeature::StartNewGame{}, AppFeature::ContinueGame{},
                   AppFeature::OpenMultiplayer{}, AppFeature::OpenLive{},
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
        } else if constexpr (std::is_same_v<S, MultiplayerFeature::State>) {
          for (const auto &multiplayerAction : MultiplayerFeatureView::collectActions(screen)) {
            actions.push_back(AppFeature::Multiplayer{multiplayerAction});
          }
          if (IsKeyPressed(KEY_ESCAPE)) {
            actions.push_back(AppFeature::Dismiss{});
          }
        } else if constexpr (std::is_same_v<S, LiveFeature::State>) {
          if (IsKeyPressed(KEY_ESCAPE)) {
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
        if constexpr (std::is_same_v<S, AppFeature::IntroScreen>) {
          drawIntro(introElapsed());
        } else if constexpr (std::is_same_v<S, AppFeature::MainMenuScreen>) {
          MenuView::draw("15 Puzzle", "", mainMenuButtons(state), menuSelected);
        } else if constexpr (std::is_same_v<S, AppFeature::PausedScreen>) {
          PuzzleFeatureView::drawBoardOnly(state.puzzle);
          dim();
          MenuView::draw("Paused", "", kPausedButtons, pausedSelected);
        } else if constexpr (std::is_same_v<S, AppFeature::GameOverScreen>) {
          PuzzleFeatureView::drawBoardOnly(state.puzzle);
          dim();
          MenuView::draw("Victory!", victorySubtitle(screen), kGameOverButtons, gameOverSelected);
        } else if constexpr (std::is_same_v<S, SettingsFeature::State>) {
          SettingsFeatureView::draw(screen);
        } else if constexpr (std::is_same_v<S, LeaderboardFeature::State>) {
          LeaderboardFeatureView::draw(screen);
        } else if constexpr (std::is_same_v<S, MultiplayerFeature::State>) {
          MultiplayerFeatureView::draw(screen);
        } else if constexpr (std::is_same_v<S, LiveFeature::State>) {
          LiveFeatureView::draw(screen);
        }
      },
      *state.destination);
}

} // namespace AppFeatureView
