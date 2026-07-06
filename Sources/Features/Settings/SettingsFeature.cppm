export module SettingsFeature;

import std;
import ComposableArchitecture;
import Sharing;
import AppSettings;

// The settings screen as a scoped sub-feature. Its State carries the app's
// `Shared<Settings>` (the same value the puzzle holds); each action mutates it
// and persists off the main thread. AppFeature copies the value back into the
// puzzle on dismiss, keeping a single source of truth.
export namespace SettingsFeature {

struct State {
  Sharing::Shared<AppSettings::Settings> settings;

  bool operator==(const State &) const = default;
};

struct SoundToggled {};
struct AutoResumeToggled {};
struct BoardSizeSelected {
  int grid = 4;
};
struct PlayerNameChanged {
  std::string name;
};
struct FullscreenToggled {};
struct ResolutionSelected {
  int width = 0;
  int height = 0;
};

using Action = std::variant<SoundToggled, AutoResumeToggled, BoardSizeSelected, PlayerNameChanged,
                            FullscreenToggled, ResolutionSelected>;

State initialState(Sharing::Shared<AppSettings::Settings> settings = {});
ComposableArchitecture::Feature<State, Action> body();

} // namespace SettingsFeature
