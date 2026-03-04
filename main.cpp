#include <raylib.h>

#include <type_traits>

#include "Core/Store.hpp"
#include "Features/App/AppFeature.hpp"
#include "Features/App/AppFeatureView.hpp"
#include "Features/Puzzle/PuzzleFeatureAudio.hpp"
#include "Features/Puzzle/PuzzleFeature.hpp"

int main() {
  SetConfigFlags(FLAG_VSYNC_HINT);
  InitWindow(
      static_cast<int>(PuzzleFeature::Config::grid * PuzzleFeature::Config::cardSize),
      static_cast<int>(PuzzleFeature::Config::grid * PuzzleFeature::Config::cardSize + PuzzleFeature::Config::uiHeight),
      "15 Puzzle");

  auto effectRunner = [](const std::vector<AppFeature::Effect>& effects, const AppFeature::Dispatch& dispatch) {
    for (const auto& effect : effects) {
      std::visit(
          [&](auto&& value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, AppFeature::PuzzleEffectProduced>) {
              std::visit(
                  [&](auto&& puzzleEffect) {
                    using PuzzleEffect = std::decay_t<decltype(puzzleEffect)>;
                    if constexpr (std::is_same_v<PuzzleEffect, PuzzleFeature::StartTimerRequested>) {
                      dispatch(AppFeature::PuzzleActionReceived{PuzzleFeature::TimerStarted{GetTime()}});
                    }
                  },
                  value.effect);
            }
          },
          effect);
    }
  };

  Store<AppFeature::State, AppFeature::Action, AppFeature::Effect> store(
      AppFeature::makeInitialState(),
      [](const AppFeature::State& state, const AppFeature::Action& action) {
        return AppFeature::reduce(state, action, GetTime());
      },
      effectRunner);

  store.send(AppFeature::AppLaunched{});

  PuzzleFeatureAudio puzzleAudio;

  while (!WindowShouldClose()) {
    const double nowSeconds = GetTime();

    for (const auto& action : AppFeatureView::collectActions(store.state, nowSeconds)) {
      store.send(action);
    }

    puzzleAudio.update(store.state.isTickSoundEnabled, store.state.puzzle, nowSeconds);

    BeginDrawing();
    ClearBackground(BLACK);
    AppFeatureView::draw(store.state, nowSeconds);
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
