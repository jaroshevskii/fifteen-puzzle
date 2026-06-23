export module AppSettings;

import std;

// The app's persisted settings, shared across features via a
// `Sharing::Shared<Settings>`. Plain value type; the JSON glue and the concrete
// file-storage strategy live in `AppSettingsLive`.
export namespace AppSettings {

struct Settings {
  bool isSoundEnabled = false;
  int lastBoardSize = 4; // PuzzleFeature::Config::minGrid (kept literal to
                         // avoid a dependency cycle with the feature)
  std::string playerName = "Player";
  // When true, the app resumes the unfinished game straight away on launch;
  // when false it shows the menu with a Continue option. Off by default.
  bool autoResume = false;

  bool operator==(const Settings &) const = default;
};

} // namespace AppSettings
