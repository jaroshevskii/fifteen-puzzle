module;

#include <raylib.h>

export module LeaderboardFeatureView;

import std;
import LeaderboardFeature;

export namespace LeaderboardFeatureView {

std::vector<LeaderboardFeature::Action> collectActions(const LeaderboardFeature::State &state);
void draw(const LeaderboardFeature::State &state);

} // namespace LeaderboardFeatureView

// --- Implementation
// -----------------------------------------------------------

namespace LeaderboardFeatureView {

std::vector<LeaderboardFeature::Action>
collectActions(const LeaderboardFeature::State & /*state*/) {
  std::vector<LeaderboardFeature::Action> actions;
  if (IsKeyPressed(KEY_L)) {
    actions.push_back(LeaderboardFeature::VisibilityToggled{});
  }
  return actions;
}

void draw(const LeaderboardFeature::State &state) {
  if (!state.isVisible) {
    return;
  }

  const int width = GetScreenWidth();
  const int height = GetScreenHeight();
  DrawRectangle(0, 0, width, height, Color{0, 0, 0, 222});

  DrawText("Leaderboard", 16, 12, 28, WHITE);
  const std::string subtitle = std::format("{}x{}   (L to close)", state.gridSize, state.gridSize);
  DrawText(subtitle.c_str(), 16, 44, 16, GRAY);

  int y = 78;
  if (state.merged.empty()) {
    DrawText(state.isLoadingRemote || state.isLoadingLocal ? "Loading…" : "No scores yet", 16, y,
             18, GRAY);
  } else {
    int rank = 1;
    for (const auto &entry : state.merged) {
      const int minutes = entry.duration / 60;
      const int seconds = entry.duration % 60;
      const std::string line = std::format("{:>2}. {:<10} {:02}:{:02}  {} mv", rank, entry.name,
                                           minutes, seconds, entry.moves);
      DrawText(line.c_str(), 16, y, 18, WHITE);
      y += 24;
      ++rank;
    }
  }

  if (state.remoteError.has_value()) {
    DrawText("offline — showing local scores", 16, height - 28, 16, ORANGE);
  }
}

} // namespace LeaderboardFeatureView
