#include "Features/App/AppFeatureView.hpp"

#include <raylib.h>

#include "Features/Puzzle/PuzzleFeatureView.hpp"

namespace AppFeatureView {

std::vector<AppFeature::Action> collectActions(const AppFeature::State& state, double nowSeconds) {
  std::vector<AppFeature::Action> actions;

  if (IsKeyPressed(KEY_M)) {
    actions.push_back(AppFeature::SoundToggleKeyPressed{});
  }

  for (const auto& puzzleAction : PuzzleFeatureView::collectActions(state.puzzle, nowSeconds)) {
    actions.push_back(AppFeature::PuzzleActionReceived{puzzleAction});
  }

  return actions;
}

void draw(const AppFeature::State& state, double nowSeconds) {
  PuzzleFeatureView::draw(state.puzzle, nowSeconds);
}

}  // namespace AppFeatureView
