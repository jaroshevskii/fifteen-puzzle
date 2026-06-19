#include <raylib.h>

import std;
import ComposableArchitecture;
import AudioPlayerClient;
import AudioPlayerClientLive;
import AppFeature;
import AppFeatureView;
import PuzzleFeature;

using ComposableArchitecture::Store;
using Dependencies::DependencyValues;
using Dependencies::prepareDependencies;

int main() {
  // Wire up live dependencies as early as possible, the analog of TCA's
  // `prepareDependencies` at the app entry point.
  prepareDependencies([](DependencyValues& values) {
    values.set<AudioPlayerClient::Key>(AudioPlayerClient::live());
  });

  SetConfigFlags(FLAG_VSYNC_HINT);
  InitWindow(
      static_cast<int>(PuzzleFeature::Config::grid * PuzzleFeature::Config::cardSize),
      static_cast<int>(PuzzleFeature::Config::grid * PuzzleFeature::Config::cardSize + PuzzleFeature::Config::uiHeight),
      "15 Puzzle");

  Store<AppFeature::State, AppFeature::Action> store(AppFeature::initialState(), AppFeature::body);

  store.send(AppFeature::Puzzle{PuzzleFeature::AppLaunched{}});

  while (!WindowShouldClose()) {
    for (const auto& action : AppFeatureView::collectActions(store.state())) {
      store.send(action);
    }

    BeginDrawing();
    ClearBackground(BLACK);
    AppFeatureView::draw(store.state());
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
