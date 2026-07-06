module;

// The JSON library is included here, in an implementation unit's global module
// fragment, so it stays private to this TU and is not reachable by importers of
// AppSettingsLive (which would otherwise clash with `import std`).
#include <nlohmann/json.hpp>

module AppSettingsLive; // implementation unit

import std;
import Sharing;
import AppSettings;

namespace AppSettings {

namespace {

std::string encode(const Settings &settings) {
  const nlohmann::json doc{
      {"isSoundEnabled", settings.isSoundEnabled}, {"lastBoardSize", settings.lastBoardSize},
      {"playerName", settings.playerName},         {"autoResume", settings.autoResume},
      {"fullscreen", settings.fullscreen},         {"displayWidth", settings.displayWidth},
      {"displayHeight", settings.displayHeight}};
  return doc.dump(2);
}

std::optional<Settings> decode(std::string_view text) {
  try {
    const auto doc = nlohmann::json::parse(text);
    Settings settings;
    settings.isSoundEnabled = doc.value("isSoundEnabled", false);
    settings.lastBoardSize = doc.value("lastBoardSize", 4);
    settings.playerName = doc.value("playerName", std::string("Player"));
    settings.autoResume = doc.value("autoResume", false);
    settings.fullscreen = doc.value("fullscreen", false);
    settings.displayWidth = doc.value("displayWidth", 960);
    settings.displayHeight = doc.value("displayHeight", 720);
    return settings;
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace

Sharing::PersistenceStrategy<Settings> settingsFileStorage(std::filesystem::path path) {
  return Sharing::fileStorage<Settings>(std::move(path), &encode, &decode);
}

} // namespace AppSettings
