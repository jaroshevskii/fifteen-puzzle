module;

#include <raylib.h>

export module AppFeatureView;

import std;
import AppFeature;
import PuzzleFeature;
import PuzzleFeatureView;
import LeaderboardFeature;
import LeaderboardFeatureView;

export namespace AppFeatureView {

std::vector<AppFeature::Action> collectActions(const AppFeature::State &state);
void draw(const AppFeature::State &state);

} // namespace AppFeatureView

// --- Implementation
// -----------------------------------------------------------

namespace AppFeatureView {

std::vector<AppFeature::Action> collectActions(const AppFeature::State &state) {
  std::vector<AppFeature::Action> actions;

  if (IsKeyPressed(KEY_M)) {
    actions.push_back(
        AppFeature::Puzzle{PuzzleFeature::SoundToggleButtonTapped{}});
  }

  for (const auto &puzzleAction :
       PuzzleFeatureView::collectActions(state.puzzle)) {
    actions.push_back(AppFeature::Puzzle{puzzleAction});
  }

  for (const auto &leaderboardAction :
       LeaderboardFeatureView::collectActions(state.leaderboard)) {
    actions.push_back(AppFeature::Leaderboard{leaderboardAction});
  }

  return actions;
}

void draw(const AppFeature::State &state) {
  PuzzleFeatureView::draw(state.puzzle);
  LeaderboardFeatureView::draw(state.leaderboard); // overlay when visible
}

} // namespace AppFeatureView
