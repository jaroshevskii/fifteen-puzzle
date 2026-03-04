#include "Features/Puzzle/PuzzleFeatureView.hpp"

#include <format>

#include <raylib.h>

namespace PuzzleFeatureView {

namespace {

Rectangle rectangleForIndex(int index) {
  const int row = index / PuzzleFeature::Config::grid;
  const int col = index % PuzzleFeature::Config::grid;
  return {
      static_cast<float>(col) * PuzzleFeature::Config::cardSize,
      static_cast<float>(row) * PuzzleFeature::Config::cardSize,
      PuzzleFeature::Config::cardSize,
      PuzzleFeature::Config::cardSize};
}

void drawCard(const std::string& text, Rectangle rect) {
  DrawRectangleRec(rect, BLACK);

  const Rectangle body{
      rect.x + 2.0f,
      rect.y + 2.0f,
      rect.width - 4.0f,
      rect.height - 4.0f};

  const Color bodyColor = text.empty() ? DARKPURPLE : ORANGE;
  DrawRectangleRec(body, bodyColor);

  if (!text.empty()) {
    constexpr int fontSize = 50;
    const int textWidth = MeasureText(text.c_str(), fontSize);
    const int x = static_cast<int>(rect.x + (rect.width - static_cast<float>(textWidth)) / 2.0f);
    const int y = static_cast<int>(rect.y + (rect.height - static_cast<float>(fontSize)) / 2.0f);
    DrawText(text.c_str(), x, y, fontSize, BLACK);
  }
}

void drawBoard(const std::vector<std::string>& tiles) {
  DrawRectangle(
      0,
      0,
      static_cast<int>(PuzzleFeature::Config::grid * PuzzleFeature::Config::cardSize),
      static_cast<int>(PuzzleFeature::Config::grid * PuzzleFeature::Config::cardSize),
      DARKPURPLE);

  for (int index = 0; index < PuzzleFeature::Config::tileCount; ++index) {
    drawCard(tiles[index], rectangleForIndex(index));
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

void drawTimerLabel(const PuzzleFeature::State& state, double nowSeconds) {
  const int totalSeconds = PuzzleFeature::elapsedSeconds(state, nowSeconds);
  const int hours = totalSeconds / 3600;
  const int minutes = (totalSeconds % 3600) / 60;
  const int seconds = totalSeconds % 60;

  const std::string prefix = state.isEnd ? "Victory Time " : "";
  const std::string label = std::format("{}{:02}:{:02}:{:02}", prefix, hours, minutes, seconds);
  DrawText(label.c_str(), 16, GetScreenHeight() - 40, 30, WHITE);
}

}  // namespace

std::vector<PuzzleFeature::Action> collectActions(const PuzzleFeature::State& state, double nowSeconds) {
  std::vector<PuzzleFeature::Action> actions;

  if (IsKeyPressed(KEY_R)) {
    actions.push_back(PuzzleFeature::RestartButtonTapped{});
  }

  if (!state.isEnd) {
    if (IsKeyPressed(KEY_S)) {
      actions.push_back(PuzzleFeature::ShuffleButtonTapped{});
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
      const Vector2 mousePosition = GetMousePosition();
      for (int index = 0; index < static_cast<int>(state.tiles.size()); ++index) {
        if (CheckCollisionPointRec(mousePosition, rectangleForIndex(index))) {
          actions.push_back(PuzzleFeature::TileTapped{index});
          break;
        }
      }
    }

    if (IsKeyPressed(KEY_UP)) {
      if (const auto index = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::up); index.has_value()) {
        actions.push_back(PuzzleFeature::TileTapped{*index});
      }
    }
    if (IsKeyPressed(KEY_DOWN)) {
      if (const auto index = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::down); index.has_value()) {
        actions.push_back(PuzzleFeature::TileTapped{*index});
      }
    }
    if (IsKeyPressed(KEY_LEFT)) {
      if (const auto index = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::left); index.has_value()) {
        actions.push_back(PuzzleFeature::TileTapped{*index});
      }
    }
    if (IsKeyPressed(KEY_RIGHT)) {
      if (const auto index = PuzzleFeature::movableTileIndex(state, PuzzleFeature::Direction::right); index.has_value()) {
        actions.push_back(PuzzleFeature::TileTapped{*index});
      }
    }

    static double lastWPressedAt = 0.0;
    constexpr double doublePressThreshold = 0.4;
    if (IsKeyPressed(KEY_W)) {
      if (nowSeconds - lastWPressedAt < doublePressThreshold) {
        actions.push_back(PuzzleFeature::NearWinShortcutActivated{});
        lastWPressedAt = 0.0;
      } else {
        lastWPressedAt = nowSeconds;
      }
    }
  } else {
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
      actions.push_back(PuzzleFeature::RestartButtonTapped{});
    }
  }

  return actions;
}

void draw(const PuzzleFeature::State& state, double nowSeconds) {
  drawBoard(state.tiles);
  if (state.isEnd) {
    drawOverlay();
  }
  drawTimerLabel(state, nowSeconds);
}

}  // namespace PuzzleFeatureView
