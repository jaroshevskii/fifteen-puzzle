module;

#include <raylib.h>

export module LiveFeatureView;

import std;
import LiveFeature;

export namespace LiveFeatureView {

void draw(const LiveFeature::State &state);

} // namespace LiveFeatureView

namespace LiveFeatureView {

namespace {

void centered(std::string_view text, int y, int fontSize, Color color) {
  const std::string label(text);
  DrawText(label.c_str(), (GetScreenWidth() - MeasureText(label.c_str(), fontSize)) / 2, y,
           fontSize, color);
}

} // namespace

void draw(const LiveFeature::State &state) {
  centered("Live Games", 24, 40, WHITE);

  if (state.phase == LiveFeature::Phase::connecting) {
    centered("Connecting to server…   (Esc to leave)", GetScreenHeight() / 2, 18, GRAY);
    return;
  }
  if (state.phase == LiveFeature::Phase::failed) {
    centered("Server unreachable", GetScreenHeight() / 2 - 12, 24, WHITE);
    centered("Esc to leave", GetScreenHeight() / 2 + 24, 18, GRAY);
    return;
  }

  // Presence line.
  const std::string presence = std::format("{} online   ·   {} racing   ·   {} in queue",
                                           state.online, state.racing, state.waiting);
  centered(presence, 76, 20, Color{120, 200, 120, 255});

  // In-progress matches.
  int y = 120;
  DrawText("In progress", 40, y, 22, WHITE);
  y += 34;
  if (state.matches.empty()) {
    DrawText("No games in progress — start one from Multiplayer!", 40, y, 18, GRAY);
    y += 30;
  } else {
    for (const auto &match : state.matches) {
      const std::string row = std::format("{}x{}   {}  vs  {}", match.gridSize, match.gridSize,
                                          match.playerA, match.playerB);
      DrawText(row.c_str(), 48, y, 18, ORANGE);
      y += 28;
    }
  }

  // Recent results ticker.
  y += 20;
  DrawText("Recent finishes", 40, y, 22, WHITE);
  y += 34;
  for (const auto &done : state.recent) {
    const std::string row =
        std::format("{} won a {}x{} race in {:02}:{:02}", done.winnerName, done.gridSize,
                    done.gridSize, done.durationSeconds / 60, done.durationSeconds % 60);
    DrawText(row.c_str(), 48, y, 16, Color{170, 170, 170, 255});
    y += 24;
  }

  centered("Esc to leave", GetScreenHeight() - 30, 16, GRAY);
}

} // namespace LiveFeatureView
