#pragma once

#include <vector>

#include "Features/Puzzle/PuzzleFeature.hpp"

namespace PuzzleFeatureView {

std::vector<PuzzleFeature::Action> collectActions(const PuzzleFeature::State& state, double nowSeconds);
void draw(const PuzzleFeature::State& state, double nowSeconds);

}  // namespace PuzzleFeatureView
