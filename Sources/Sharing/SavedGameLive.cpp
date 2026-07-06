module;

#include <nlohmann/json.hpp>

module SavedGameLive; // implementation unit

import std;
import Sharing;
import SavedGame;

namespace SavedGame {

namespace {

std::string encode(const Game &game) {
  const nlohmann::json doc{{"grid", game.grid},
                           {"tiles", game.tiles},
                           {"moveHistory", game.moveHistory},
                           {"secondsElapsed", game.secondsElapsed}};
  return doc.dump(2);
}

std::optional<Game> decode(std::string_view text) {
  try {
    const auto doc = nlohmann::json::parse(text);
    Game game;
    game.grid = doc.value("grid", 4);
    game.tiles = doc.value("tiles", std::vector<std::string>{});
    game.moveHistory = doc.value("moveHistory", std::vector<int>{});
    game.secondsElapsed = doc.value("secondsElapsed", 0);
    if (game.tiles.empty()) {
      return std::nullopt; // not a usable board
    }
    return game;
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace

Sharing::PersistenceStrategy<std::optional<Game>> savedGameFileStorage(std::filesystem::path path) {
  return Sharing::PersistenceStrategy<std::optional<Game>>{
      .load = [path]() -> std::optional<std::optional<Game>> {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
          return std::nullopt;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
          return std::nullopt;
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        auto game = decode(text);
        if (!game) {
          return std::nullopt; // corrupt → treat as absent
        }
        return std::optional<Game>(std::move(*game));
      },
      .save =
          [path](const std::optional<Game> &value) {
            std::error_code ec;
            if (!value) {
              std::filesystem::remove(path, ec); // clear-on-win
              return;
            }
            try {
              if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path(), ec);
              }
              const std::string text = encode(*value);
              const std::filesystem::path tmp = std::filesystem::path(path).concat(".tmp");
              {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out) {
                  return;
                }
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
              }
              std::filesystem::rename(tmp, path, ec);
            } catch (...) {
            }
          }};
}

} // namespace SavedGame
