module;

#include <raylib.h>

export module PuzzleFeatureView;

import std;
import PuzzleFeature;

export namespace PuzzleFeatureView {

std::vector<PuzzleFeature::Action> collectActions(const PuzzleFeature::State& state);
void draw(const PuzzleFeature::State& state);

}  // namespace PuzzleFeatureView

// --- Implementation -----------------------------------------------------------

namespace PuzzleFeatureView {

namespace {

namespace Config = PuzzleFeature::Config;

Rectangle rectangleForIndex(int index, int grid) {
  const float size = Config::tileSize(grid);
  const int row = index / grid;
  const int col = index % grid;
  return {static_cast<float>(col) * size, static_cast<float>(row) * size, size, size};
}

void drawCard(const std::string& text, Rectangle rect, int grid) {
  DrawRectangleRec(rect, BLACK);
  const Rectangle body{rect.x + 2.0f, rect.y + 2.0f, rect.width - 4.0f, rect.height - 4.0f};
  DrawRectangleRec(body, text.empty() ? DARKPURPLE : ORANGE);

  if (!text.empty()) {
    int fontSize = Config::fontSize(grid);
    // Shrink to fit wide labels on large boards (raylib fonts are integer-sized).
    while (fontSize > 8 && MeasureText(text.c_str(), fontSize) > static_cast<int>(rect.width) - 8) {
      --fontSize;
    }
    const int x = static_cast<int>(rect.x + (rect.width - static_cast<float>(MeasureText(text.c_str(), fontSize))) / 2.0f);
    const int y = static_cast<int>(rect.y + (rect.height - static_cast<float>(fontSize)) / 2.0f);
    DrawText(text.c_str(), x, y, fontSize, BLACK);
  }
}

void drawBoard(const PuzzleFeature::State& state) {
  const int boardPixels = static_cast<int>(Config::boardPixels(state.grid));
  DrawRectangle(0, 0, boardPixels, boardPixels, DARKPURPLE);
  for (int index = 0; index < static_cast<int>(state.tiles.size()); ++index) {
    drawCard(state.tiles[index], rectangleForIndex(index, state.grid), state.grid);
  }
}

void drawOverlay() {
  const int width = GetScreenWidth();
  const int height = GetScreenHeight();
  DrawRectangle(0, 0, width, height, Color{0, 0, 0, 192});
  const char* title = "Victory!";
  constexpr int titleFontSize = 60;
  DrawText(title, (width - MeasureText(title, titleFontSize)) / 2, (height - titleFontSize) / 2 - 32, titleFontSize, WHITE);
  const char* subtitle = "Click or press R to continue.";
  DrawText(subtitle, (width - MeasureText(subtitle, 20)) / 2, (height + titleFontSize) / 2, 20, WHITE);
}

void drawSolvingBanner(const PuzzleFeature::State& state) {
  DrawRectangle(0, 0, GetScreenWidth(), 28, Color{0, 0, 0, 180});
  const char* label = state.pendingMoves.empty() ? "Solving…" : "Auto-solving — press H or click to stop";
  DrawText(label, 10, 6, 16, GREEN);
}

void drawStatusLabel(const PuzzleFeature::State& state) {
  const int totalSeconds = PuzzleFeature::displayedSeconds(state);
  const int hours = totalSeconds / 3600;
  const int minutes = (totalSeconds % 3600) / 60;
  const int seconds = totalSeconds % 60;
  const std::string prefix = state.isGameOver ? "Victory Time " : "";
  const std::string label =
      std::format("{}{:02}:{:02}:{:02}   {}x{}  (0-9 resize)", prefix, hours, minutes, seconds, state.grid, state.grid);
  DrawText(label.c_str(), 16, GetScreenHeight() - 40, 24, WHITE);
}

}  // namespace

std::vector<PuzzleFeature::Action> collectActions(const PuzzleFeature::State& state) {
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
      if (const auto i = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::down); i) {
        actions.push_back(PuzzleFeature::TileTapped{*i});
      }
    }
    if (IsKeyPressed(KEY_LEFT)) {
      if (const auto i = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::left); i) {
        actions.push_back(PuzzleFeature::TileTapped{*i});
      }
    }
    if (IsKeyPressed(KEY_RIGHT)) {
      if (const auto i = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::right); i) {
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

void draw(const PuzzleFeature::State& state) {
  drawBoard(state);
  if (state.isGameOver) {
    drawOverlay();
  }
  if (state.isSolving) {
    drawSolvingBanner(state);
  }
  drawStatusLabel(state);
}

}  // namespace PuzzleFeatureView
