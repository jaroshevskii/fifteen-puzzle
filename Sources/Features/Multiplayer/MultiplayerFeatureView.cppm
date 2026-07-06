module;

#include <raylib.h>

export module MultiplayerFeatureView;

import std;
import MultiplayerFeature;
import PuzzleCore;

export namespace MultiplayerFeatureView {

std::vector<MultiplayerFeature::Action> collectActions(const MultiplayerFeature::State &state);
void draw(const MultiplayerFeature::State &state);

} // namespace MultiplayerFeatureView

// --- Implementation
// -----------------------------------------------------------

namespace MultiplayerFeatureView {

namespace {

using MultiplayerFeature::Phase;

constexpr float kHudHeight = 72.0f; // reserved above the board for the race HUD
constexpr float kMaxTile = 120.0f;

struct BoardLayout {
  float originX;
  float originY;
  float tile;
};

BoardLayout boardLayout(int grid) {
  const float screenW = static_cast<float>(GetScreenWidth());
  const float screenH = static_cast<float>(GetScreenHeight());
  const float available = std::min(screenW, screenH - kHudHeight * 2.0f) * 0.92f;
  const float tile = std::min(kMaxTile, available / static_cast<float>(grid));
  const float board = tile * static_cast<float>(grid);
  return {(screenW - board) / 2.0f, kHudHeight + (screenH - kHudHeight * 2.0f - board) / 2.0f,
          tile};
}

Rectangle rectangleForIndex(int index, int grid) {
  const BoardLayout layout = boardLayout(grid);
  const int row = index / grid;
  const int col = index % grid;
  return {layout.originX + static_cast<float>(col) * layout.tile,
          layout.originY + static_cast<float>(row) * layout.tile, layout.tile, layout.tile};
}

void drawCard(const std::string &text, Rectangle rect) {
  DrawRectangleRec(rect, BLACK);
  const Rectangle body{rect.x + 2.0f, rect.y + 2.0f, rect.width - 4.0f, rect.height - 4.0f};
  DrawRectangleRec(body, text.empty() ? DARKPURPLE : ORANGE);

  if (!text.empty()) {
    int fontSize = std::max(10, static_cast<int>(rect.height * 0.5f));
    while (fontSize > 8 && MeasureText(text.c_str(), fontSize) > static_cast<int>(rect.width) - 8) {
      --fontSize;
    }
    const int x = static_cast<int>(
        rect.x + (rect.width - static_cast<float>(MeasureText(text.c_str(), fontSize))) / 2.0f);
    const int y = static_cast<int>(rect.y + (rect.height - static_cast<float>(fontSize)) / 2.0f);
    DrawText(text.c_str(), x, y, fontSize, BLACK);
  }
}

// Tile that would slide into the empty cell for an arrow key (same feel as the
// solo puzzle: Up moves the tile below the hole up, and so on).
std::optional<int> movableTileIndex(const MultiplayerFeature::State &state, int keyPressed) {
  const auto empty = PuzzleCore::emptyIndex(state.tiles);
  if (!empty.has_value()) {
    return std::nullopt;
  }
  const int grid = state.gridSize;
  const int row = *empty / grid;
  const int col = *empty % grid;
  switch (keyPressed) {
  case KEY_UP:
    if (row < grid - 1)
      return (row + 1) * grid + col;
    break;
  case KEY_DOWN:
    if (row > 0)
      return (row - 1) * grid + col;
    break;
  case KEY_LEFT:
    if (col < grid - 1)
      return row * grid + (col + 1);
    break;
  case KEY_RIGHT:
    if (col > 0)
      return row * grid + (col - 1);
    break;
  default:
    break;
  }
  return std::nullopt;
}

void drawCenteredText(std::string_view text, int y, int fontSize, Color color) {
  const std::string label(text);
  DrawText(label.c_str(), (GetScreenWidth() - MeasureText(label.c_str(), fontSize)) / 2, y,
           fontSize, color);
}

void drawStatusScreen(std::string_view title, std::string_view subtitle) {
  const int height = GetScreenHeight();
  drawCenteredText(title, height / 2 - 40, 40, WHITE);
  drawCenteredText(subtitle, height / 2 + 16, 18, GRAY);
}

void drawRaceHud(const MultiplayerFeature::State &state) {
  const std::string clock =
      std::format("{:02}:{:02}", state.secondsElapsed / 60, state.secondsElapsed % 60);
  drawCenteredText(clock, 10, 28, WHITE);

  const std::string you = std::format("{}: {} moves", state.playerName, state.moveHistory.size());
  DrawText(you.c_str(), 12, 44, 18, GREEN);

  const std::string opponent =
      std::format("{}: {} moves", state.opponentName, state.opponentMoveCount);
  DrawText(opponent.c_str(), GetScreenWidth() - MeasureText(opponent.c_str(), 18) - 12, 44, 18,
           ORANGE);
}

// A small live copy of the opponent's board in the top-right corner — their
// (referee-validated) moves are replayed on it as they arrive, so you can
// watch the race, not just a move counter.
void drawOpponentPreview(const MultiplayerFeature::State &state) {
  if (state.opponentTiles.empty()) {
    return;
  }
  const int grid = state.gridSize;
  const float preview = std::min(150.0f, static_cast<float>(GetScreenWidth()) * 0.22f);
  const float tile = preview / static_cast<float>(grid);
  const float originX = static_cast<float>(GetScreenWidth()) - preview - 12.0f;
  const float originY = kHudHeight;

  // Backing panel so the preview reads over the main board on narrow windows.
  DrawRectangleRec({originX - 4.0f, originY - 4.0f, preview + 8.0f, preview + 8.0f},
                   Color{20, 20, 26, 230});

  const bool solved = PuzzleCore::isSolved(state.opponentTiles, grid);
  for (int index = 0; index < static_cast<int>(state.opponentTiles.size()); ++index) {
    const int row = index / grid;
    const int col = index % grid;
    const Rectangle cell{originX + static_cast<float>(col) * tile + 1.0f,
                         originY + static_cast<float>(row) * tile + 1.0f, tile - 2.0f, tile - 2.0f};
    if (state.opponentTiles[static_cast<std::size_t>(index)].empty()) {
      DrawRectangleRec(cell, Color{40, 30, 55, 255}); // the hole
    } else {
      DrawRectangleRec(cell, solved ? GREEN : Color{200, 110, 40, 255});
      // Tiny tiles read better as color blocks; label them when they fit.
      if (tile >= 22.0f) {
        const std::string &label = state.opponentTiles[static_cast<std::size_t>(index)];
        const int font = std::max(8, static_cast<int>(tile * 0.45f));
        DrawText(label.c_str(),
                 static_cast<int>(
                     cell.x +
                     (cell.width - static_cast<float>(MeasureText(label.c_str(), font))) / 2.0f),
                 static_cast<int>(cell.y + (cell.height - static_cast<float>(font)) / 2.0f), font,
                 BLACK);
      }
    }
  }

  DrawText(state.opponentName.c_str(), static_cast<int>(originX),
           static_cast<int>(originY + preview + 8.0f), 14, ORANGE);
}

} // namespace

std::vector<MultiplayerFeature::Action> collectActions(const MultiplayerFeature::State &state) {
  std::vector<MultiplayerFeature::Action> actions;

  actions.push_back(MultiplayerFeature::TimerTicked{});

  switch (state.phase) {
  case Phase::racing: {
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
      const Vector2 mouse = GetMousePosition();
      for (int index = 0; index < static_cast<int>(state.tiles.size()); ++index) {
        if (CheckCollisionPointRec(mouse, rectangleForIndex(index, state.gridSize))) {
          actions.push_back(MultiplayerFeature::TileTapped{index});
          break;
        }
      }
    }
    for (const int key : {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT}) {
      if (IsKeyPressed(key)) {
        if (const auto index = movableTileIndex(state, key)) {
          actions.push_back(MultiplayerFeature::TileTapped{*index});
        }
      }
    }
    break;
  }
  case Phase::finished:
  case Phase::failed:
    if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ENTER)) {
      actions.push_back(MultiplayerFeature::RematchTapped{});
    }
    break;
  case Phase::connecting:
  case Phase::queued:
    break;
  }

  return actions;
}

void draw(const MultiplayerFeature::State &state) {
  switch (state.phase) {
  case Phase::connecting:
    drawStatusScreen("Multiplayer", "Connecting to server…   (Esc to leave)");
    return;
  case Phase::queued:
    drawStatusScreen("Multiplayer", "Waiting for an opponent…   (Esc to leave)");
    return;
  case Phase::failed:
    drawStatusScreen("Connection lost", "R / Enter to retry — Esc to leave");
    return;
  case Phase::finished: {
    const std::string title = state.youWon ? "You won!" : "You lost";
    const std::string subtitle =
        state.opponentLeft
            ? "Your opponent left the race — R / Enter for a new match, Esc to leave"
            : std::format("{} solved it in {:02}:{:02} — R / Enter for a rematch, Esc to leave",
                          state.winnerName, state.finalDurationSeconds / 60,
                          state.finalDurationSeconds % 60);
    drawStatusScreen(title, subtitle);
    return;
  }
  case Phase::racing:
    break;
  }

  // The race: HUD on top, this player's board centered below, the opponent's
  // live mini board in the corner.
  drawRaceHud(state);

  const BoardLayout layout = boardLayout(state.gridSize);
  const float board = layout.tile * static_cast<float>(state.gridSize);
  DrawRectangleRec({layout.originX, layout.originY, board, board}, DARKPURPLE);
  for (int index = 0; index < static_cast<int>(state.tiles.size()); ++index) {
    drawCard(state.tiles[index], rectangleForIndex(index, state.gridSize));
  }
  drawOpponentPreview(state);

  if (MultiplayerFeature::isBoardSolved(state)) {
    drawCenteredText("Solved — waiting for the referee…", GetScreenHeight() - 40, 20, GREEN);
  }
}

} // namespace MultiplayerFeatureView
