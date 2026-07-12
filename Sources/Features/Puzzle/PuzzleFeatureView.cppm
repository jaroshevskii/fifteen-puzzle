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

constexpr float kStatusHeight = 44.0f; // reserved for the status label
constexpr float kMaxTile = 120.0f;     // cap so small boards aren't huge on big screens

// The board is centered in the window and scaled so it always fits the current
// resolution (shrinking when the grid is large or the screen is small).
struct BoardLayout {
  float originX;
  float originY;
  float tile;
};

BoardLayout boardLayout(int grid) {
  const float screenW = static_cast<float>(GetScreenWidth());
  const float screenH = static_cast<float>(GetScreenHeight());
  const float available = std::min(screenW, screenH - kStatusHeight) * 0.92f;
  const float tile = std::min(kMaxTile, available / static_cast<float>(grid));
  const float board = tile * static_cast<float>(grid);
  return {(screenW - board) / 2.0f, (screenH - kStatusHeight - board) / 2.0f, tile};
}

Rectangle rectangleForIndex(int index, int grid) {
  const BoardLayout layout = boardLayout(grid);
  const int row = index / grid;
  const int col = index % grid;
  return {layout.originX + static_cast<float>(col) * layout.tile,
          layout.originY + static_cast<float>(row) * layout.tile, layout.tile, layout.tile};
}

void drawCard(const std::string &text, Rectangle rect, unsigned char alpha = 255) {
  const float fade = static_cast<float>(alpha) / 255.0f;
  DrawRectangleRec(rect, Fade(BLACK, fade));
  const Rectangle body{rect.x + 2.0f, rect.y + 2.0f, rect.width - 4.0f, rect.height - 4.0f};
  DrawRectangleRec(body, Fade(text.empty() ? DARKPURPLE : ORANGE, fade));

  if (!text.empty()) {
    int fontSize = std::max(10, static_cast<int>(rect.height * 0.5f));
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
  const BoardLayout layout = boardLayout(state.grid);
  const float board = layout.tile * static_cast<float>(state.grid);
  DrawRectangleRec({layout.originX, layout.originY, board, board}, DARKPURPLE);

  // Sliding tiles: each tile eases from where it was last frame toward its
  // current target cell, so a move (or a reshuffle) glides instead of
  // teleporting. The animated positions live in a view-only cache keyed by tile
  // label; a change in grid size resets it so a new board snaps into place. On
  // top of the slide, freshly-appearing tiles fade+drop in (the intro feel).
  static int cachedGrid = -1;
  static std::unordered_map<std::string, Vector2> positions;
  if (state.grid != cachedGrid) {
    positions.clear();
    cachedGrid = state.grid;
  }
  // Frame-rate-independent ease toward the target (~120ms to close the gap).
  const float dt = GetFrameTime();
  const float blend = 1.0f - std::exp(-dt * 18.0f);

  const int count = state.grid * state.grid;
  constexpr float spread = 0.3f;
  constexpr float duration = 0.3f;

  // Draw the empty tile first so it always renders beneath numbered tiles
  if (auto it = std::ranges::find(state.tiles, std::string{}); it != state.tiles.end()) {
    const int holeIndex = std::distance(state.tiles.begin(), it);
    const Rectangle holeTarget = rectangleForIndex(holeIndex, state.grid);
    drawCard(state.tiles[holeIndex], holeTarget);
  }

  for (int index = 0; index < static_cast<int>(state.tiles.size()); ++index) {
    const Rectangle target = rectangleForIndex(index, state.grid);
    if (state.tiles[index].empty()) {
      continue; // already drawn
    }

    const std::string &label = state.tiles[index];
    Vector2 &pos = positions[label];
    // First sight of this label: snap to the target (no slide from origin).
    if (pos.x == 0.0f && pos.y == 0.0f) {
      pos = {target.x, target.y};
    } else {
      pos.x += (target.x - pos.x) * blend;
      pos.y += (target.y - pos.y) * blend;
    }

    // Appear fade/drop, staggered by cell, only while the board is settling in.
    const float progress = std::clamp(
        static_cast<float>((appear - static_cast<double>(index) / count * spread) / duration), 0.0f,
        1.0f);
    Rectangle rect{pos.x, pos.y, target.width, target.height};
    unsigned char alpha = 255;
    if (progress < 1.0f) {
      const float eased = easeOutCubic(progress);
      const float startY = -layout.tile;
      rect.y = startY + (rect.y - startY) * eased;
      alpha = static_cast<unsigned char>(255 * progress);
    }
    drawCard(label, rect, alpha);
  }
}

// A small confetti burst for the victory overlay — view-only, seeded once per
// win and animated by the raylib clock. `active` (re)seeds when a win begins.
void drawConfetti(bool active) {
  struct Particle {
    float x, y, vx, vy;
    Color color;
  };
  static std::vector<Particle> particles;
  static bool seeded = false;

  if (!active) {
    seeded = false;
    particles.clear();
    return;
  }
  const float width = static_cast<float>(GetScreenWidth());
  if (!seeded) {
    seeded = true;
    particles.clear();
    const Color palette[] = {ORANGE, GREEN, SKYBLUE, GOLD, PINK, VIOLET};
    for (int i = 0; i < 160; ++i) {
      particles.push_back(
          Particle{.x = static_cast<float>(GetRandomValue(0, static_cast<int>(width))),
                   .y = static_cast<float>(GetRandomValue(-400, 0)),
                   .vx = static_cast<float>(GetRandomValue(-40, 40)),
                   .vy = static_cast<float>(GetRandomValue(120, 320)),
                   .color = palette[GetRandomValue(0, 5)]});
    }
  }
  const float dt = GetFrameTime();
  const float height = static_cast<float>(GetScreenHeight());
  for (auto &p : particles) {
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    if (p.y > height) {
      p.y = static_cast<float>(GetRandomValue(-200, 0)); // recycle so it keeps raining
      p.x = static_cast<float>(GetRandomValue(0, static_cast<int>(width)));
    }
    DrawRectangleV({p.x, p.y}, {6.0f, 6.0f}, p.color);
  }
}

void drawOverlay() {
  const int width = GetScreenWidth();
  const int height = GetScreenHeight();
  DrawRectangle(0, 0, width, height, Color{0, 0, 0, 192});
  drawConfetti(true);
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
  const std::string label = std::format("{}{:02}:{:02}:{:02}   {}x{}  (0-9 resize · Esc menu)",
                                        prefix, hours, minutes, seconds, state.grid, state.grid);
  constexpr int fontSize = 22;
  DrawText(label.c_str(), (GetScreenWidth() - MeasureText(label.c_str(), fontSize)) / 2,
           GetScreenHeight() - 32, fontSize, WHITE);
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
  } else {
    drawConfetti(false); // reset the burst so the next win re-seeds fresh
  }
  if (state.isSolving) {
    drawSolvingBanner(state);
  }
  drawStatusLabel(state);
}

} // namespace PuzzleFeatureView
