module;

#include <raylib.h>

export module PuzzleFeatureView;

import std;
import PuzzleFeature;

export namespace PuzzleFeatureView {

std::vector<PuzzleFeature::Action> collectActions(const PuzzleFeature::State &state);
void draw(const PuzzleFeature::State &state);
// Just the board (no victory overlay / solving banner / status label), for use
// behind the pause and victory screens.
void drawBoardOnly(const PuzzleFeature::State &state);

} // namespace PuzzleFeatureView

// --- Implementation
// -----------------------------------------------------------

namespace PuzzleFeatureView {

namespace {

namespace Config = PuzzleFeature::Config;

Rectangle rectangleForIndex(int index, int grid) {
  const float size = Config::tileSize(grid);
  const int row = index / grid;
  const int col = index % grid;
  return {static_cast<float>(col) * size, static_cast<float>(row) * size, size, size};
}

void drawCard(const std::string &text, Rectangle rect, int grid, unsigned char alpha = 255) {
  const float fade = static_cast<float>(alpha) / 255.0f;
  DrawRectangleRec(rect, Fade(BLACK, fade));
  const Rectangle body{rect.x + 2.0f, rect.y + 2.0f, rect.width - 4.0f, rect.height - 4.0f};
  DrawRectangleRec(body, Fade(text.empty() ? DARKPURPLE : ORANGE, fade));

  if (!text.empty()) {
    int fontSize = Config::fontSize(grid);
    // Shrink to fit wide labels on large boards (raylib fonts are
    // integer-sized).
    while (fontSize > 8 && MeasureText(text.c_str(), fontSize) > static_cast<int>(rect.width) - 8) {
      --fontSize;
    }
    const int x = static_cast<int>(
        rect.x + (rect.width - static_cast<float>(MeasureText(text.c_str(), fontSize))) / 2.0f);
    const int y = static_cast<int>(rect.y + (rect.height - static_cast<float>(fontSize)) / 2.0f);
    DrawText(text.c_str(), x, y, fontSize, Fade(BLACK, fade));
  }
}

float easeOutCubic(float t) {
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}

// Elapsed seconds since the board last (re)appeared. The "appear" resets when a
// new game starts — detected by a change in (grid, startDate), guarded to
// !isGameOver so winning (which clears startDate) doesn't replay it. View-only,
// like the double-press timer below.
double boardAppearElapsed(const PuzzleFeature::State &state) {
  static int lastGrid = -1;
  static std::optional<double> lastStart;
  static double animStart = -1.0e9;
  if (!state.isGameOver && (state.grid != lastGrid || state.startDate != lastStart)) {
    lastGrid = state.grid;
    lastStart = state.startDate;
    animStart = GetTime();
  }
  return GetTime() - animStart;
}

void drawBoard(const PuzzleFeature::State &state, double appear) {
  const int boardPixels = static_cast<int>(Config::boardPixels(state.grid));
  DrawRectangle(0, 0, boardPixels, boardPixels, DARKPURPLE);

  // Tiles drop in with a staggered ease-out when the board first appears; the
  // spread is normalized by tile count so it stays ~0.6s on any board size.
  const int count = state.grid * state.grid;
  constexpr float spread = 0.3f;
  constexpr float duration = 0.3f;

  for (int index = 0; index < static_cast<int>(state.tiles.size()); ++index) {
    Rectangle rect = rectangleForIndex(index, state.grid);
    if (state.tiles[index].empty()) {
      drawCard(state.tiles[index], rect, state.grid); // the hole stays put
      continue;
    }
    const float progress = std::clamp(
        static_cast<float>((appear - static_cast<double>(index) / count * spread) / duration), 0.0f,
        1.0f);
    if (progress <= 0.0f) {
      continue; // not dropped in yet
    }
    const float eased = easeOutCubic(progress);
    rect.y = -static_cast<float>(boardPixels) + (rect.y + static_cast<float>(boardPixels)) * eased;
    drawCard(state.tiles[index], rect, state.grid, static_cast<unsigned char>(255 * progress));
  }
}

void drawOverlay() {
  const int width = GetScreenWidth();
  const int height = GetScreenHeight();
  DrawRectangle(0, 0, width, height, Color{0, 0, 0, 192});
  const char *title = "Victory!";
  constexpr int titleFontSize = 60;
  DrawText(title, (width - MeasureText(title, titleFontSize)) / 2,
           (height - titleFontSize) / 2 - 32, titleFontSize, WHITE);
  const char *subtitle = "Click or press R to continue.";
  DrawText(subtitle, (width - MeasureText(subtitle, 20)) / 2, (height + titleFontSize) / 2, 20,
           WHITE);
}

void drawSolvingBanner(const PuzzleFeature::State &state) {
  DrawRectangle(0, 0, GetScreenWidth(), 28, Color{0, 0, 0, 180});
  const char *label =
      state.pendingMoves.empty() ? "Solving…" : "Auto-solving — press H or click to stop";
  DrawText(label, 10, 6, 16, GREEN);
}

void drawStatusLabel(const PuzzleFeature::State &state) {
  const int totalSeconds = PuzzleFeature::displayedSeconds(state);
  const int hours = totalSeconds / 3600;
  const int minutes = (totalSeconds % 3600) / 60;
  const int seconds = totalSeconds % 60;
  const std::string prefix = state.isGameOver ? "Victory Time " : "";
  const std::string label = std::format("{}{:02}:{:02}:{:02}   {}x{}  (0-9 resize)", prefix, hours,
                                        minutes, seconds, state.grid, state.grid);
  DrawText(label.c_str(), 16, GetScreenHeight() - 40, 24, WHITE);
}

} // namespace

std::vector<PuzzleFeature::Action> collectActions(const PuzzleFeature::State &state) {
  std::vector<PuzzleFeature::Action> actions;

  actions.push_back(PuzzleFeature::TimerTicked{});

  // Digit keys 0..9 select the board size (level).
  for (int digit = 0; digit <= 9; ++digit) {
    if (IsKeyPressed(KEY_ZERO + digit)) {
      actions.push_back(PuzzleFeature::BoardSizeSelected{Config::gridForLevel(digit)});
    }
  }

  if (IsKeyPressed(KEY_R)) {
    actions.push_back(PuzzleFeature::RestartButtonTapped{});
  }
  if (IsKeyPressed(KEY_H)) {
    actions.push_back(PuzzleFeature::AutoSolveButtonTapped{});
  }

  if (!state.isGameOver) {
    if (IsKeyPressed(KEY_S)) {
      actions.push_back(PuzzleFeature::ShuffleButtonTapped{});
    }
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
      const Vector2 mouse = GetMousePosition();
      for (int index = 0; index < static_cast<int>(state.tiles.size()); ++index) {
        if (CheckCollisionPointRec(mouse, rectangleForIndex(index, state.grid))) {
          actions.push_back(PuzzleFeature::TileTapped{index});
          break;
        }
      }
    }
    if (IsKeyPressed(KEY_UP)) {
      if (const auto i = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::up); i) {
        actions.push_back(PuzzleFeature::TileTapped{*i});
      }
    }
    if (IsKeyPressed(KEY_DOWN)) {
      if (const auto i = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::down);
          i) {
        actions.push_back(PuzzleFeature::TileTapped{*i});
      }
    }
    if (IsKeyPressed(KEY_LEFT)) {
      if (const auto i = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::left);
          i) {
        actions.push_back(PuzzleFeature::TileTapped{*i});
      }
    }
    if (IsKeyPressed(KEY_RIGHT)) {
      if (const auto i = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::right);
          i) {
        actions.push_back(PuzzleFeature::TileTapped{*i});
      }
    }

    static double lastWPressedAt = 0.0;
    constexpr double doublePressThreshold = 0.4;
    if (IsKeyPressed(KEY_W)) {
      const double now = GetTime();
      if (now - lastWPressedAt < doublePressThreshold) {
        actions.push_back(PuzzleFeature::NearWinShortcutActivated{});
        lastWPressedAt = 0.0;
      } else {
        lastWPressedAt = now;
      }
    }
  } else if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
    actions.push_back(PuzzleFeature::RestartButtonTapped{});
  }

  return actions;
}

void drawBoardOnly(const PuzzleFeature::State &state) {
  drawBoard(state, boardAppearElapsed(state));
}

void draw(const PuzzleFeature::State &state) {
  drawBoard(state, boardAppearElapsed(state));
  if (state.isGameOver) {
    drawOverlay();
  }
  if (state.isSolving) {
    drawSolvingBanner(state);
  }
  drawStatusLabel(state);
}

} // namespace PuzzleFeatureView
