#pragma once

#include <vector>

#include "Features/App/AppFeature.hpp"

namespace AppFeatureView {

std::vector<AppFeature::Action> collectActions(const AppFeature::State& state, double nowSeconds);
void draw(const AppFeature::State& state, double nowSeconds);

}  // namespace AppFeatureView
