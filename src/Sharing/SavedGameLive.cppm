export module SavedGameLive;

import std;
import Sharing;
import SavedGame;

// Interface unit: the concrete JSON file strategy for the saved game. Unlike the
// generic `Sharing::fileStorage`, this one is hand-written so that saving
// `std::nullopt` DELETES the file — that is the clear-on-win path. JSON stays
// private to the implementation unit (SavedGameLive.cpp).
export namespace SavedGame {

Sharing::PersistenceStrategy<std::optional<Game>> savedGameFileStorage(std::filesystem::path path);

} // namespace SavedGame
