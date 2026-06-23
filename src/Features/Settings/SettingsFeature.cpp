module SettingsFeature; // implementation unit

import std;
import ComposableArchitecture;
import Sharing;
import AppSettings;

namespace SettingsFeature {

namespace {
// Board-size bounds mirror PuzzleFeature::Config (kept literal to avoid a
// dependency cycle with the feature).
constexpr int kMinGrid = 4;
constexpr int kMaxGrid = 13;
} // namespace

State initialState(Sharing::Shared<AppSettings::Settings> settings) {
  return State{.settings = std::move(settings)};
}

ComposableArchitecture::Feature<State, Action> body() {
  using FeatureStore = ComposableArchitecture::Store<State, Action>;

  return ComposableArchitecture::Update<State, Action>([](State &state, const Action &action,
                                                          FeatureStore &store) {
    // Persist the (already-mutated) settings off the main thread.
    const auto persist = [&] {
      store.addTask([strategy = state.settings.strategy(), value = state.settings.get()](
                        FeatureStore &, std::stop_token) { strategy.save(value); });
    };

    std::visit(
        [&](auto &&value) {
          using V = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<V, SoundToggled>) {
            state.settings.withMutation(
                [](AppSettings::Settings &s) { s.isSoundEnabled = !s.isSoundEnabled; });
          } else if constexpr (std::is_same_v<V, AutoResumeToggled>) {
            state.settings.withMutation(
                [](AppSettings::Settings &s) { s.autoResume = !s.autoResume; });
          } else if constexpr (std::is_same_v<V, BoardSizeSelected>) {
            state.settings.withMutation([grid = std::clamp(value.grid, kMinGrid, kMaxGrid)](
                                            AppSettings::Settings &s) { s.lastBoardSize = grid; });
          } else if constexpr (std::is_same_v<V, PlayerNameChanged>) {
            state.settings.withMutation(
                [name = value.name](AppSettings::Settings &s) { s.playerName = name; });
          }
          persist();
        },
        action);
  });
}

} // namespace SettingsFeature
