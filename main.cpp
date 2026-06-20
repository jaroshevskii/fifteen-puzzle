#include <raylib.h>

import std;
import ComposableArchitecture;
import AudioPlayerClient;
import AudioPlayerClientLive;
import SolverClient;
import SolverClientLive;
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
    values.set<SolverClient::Key>(SolverClient::live());
    // Resolve the remaining keys now so the dependency storage is fully
    // populated before any background effect thread reads from it.
    (void)values.get<Dependencies::DateGeneratorKey>();
    (void)values.get<Dependencies::RandomNumberGeneratorKey>();
  });

  namespace Config = PuzzleFeature::Config;
  SetConfigFlags(FLAG_VSYNC_HINT);
  InitWindow(Config::windowWidth(Config::minGrid), Config::windowHeight(Config::minGrid), "N Puzzle");

  Store<AppFeature::State, AppFeature::Action> store(AppFeature::initialState(), AppFeature::body);

  store.send(AppFeature::Puzzle{PuzzleFeature::AppLaunched{}});

  int displayedGrid = Config::minGrid;

  while (!WindowShouldClose()) {
    for (const auto& action : AppFeatureView::collectActions(store.state())) {
      store.send(action);
    }

    // Deliver any actions produced by background effects (e.g. the solver).
    store.pump();

    // Resize the window when the board size changes.
    const int grid = store.state().puzzle.grid;
    if (grid != displayedGrid) {
      SetWindowSize(Config::windowWidth(grid), Config::windowHeight(grid));
      displayedGrid = grid;
    }

    BeginDrawing();
    ClearBackground(BLACK);
    AppFeatureView::draw(store.state());
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
