// Tests the saved-game JSON file strategy: round-trips an in-progress game and,
// crucially, deletes the file when saving std::nullopt (the clear-on-win path).

import std;
import Sharing;
import SavedGame;
import SavedGameLive;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

void testRoundTripAndClear() {
  const auto path = std::filesystem::temp_directory_path() / "fifteen-savedgame-test.json";
  std::error_code ec;
  std::filesystem::remove(path, ec);

  auto strategy = SavedGame::savedGameFileStorage(path);

  // Missing file → load yields nothing.
  expect(!strategy.load().has_value(), "missing file → nullopt");

  const SavedGame::Game game{
      .grid = 4, .tiles = {"1", "2", "3", ""}, .moveHistory = {3, 2}, .secondsElapsed = 12};
  strategy.save(game);

  const auto loaded = strategy.load();
  expect(loaded.has_value() && loaded->has_value(), "saved game loads back");
  if (loaded && *loaded) {
    expect((*loaded)->grid == 4, "grid round-trips");
    expect((*loaded)->secondsElapsed == 12, "secondsElapsed round-trips");
    expect((*loaded)->moveHistory == std::vector<int>({3, 2}), "moveHistory round-trips");
    expect((*loaded)->tiles == std::vector<std::string>({"1", "2", "3", ""}), "tiles round-trip");
  }

  // Saving nullopt clears the slot and removes the file.
  strategy.save(std::nullopt);
  expect(!strategy.load().has_value(), "save(nullopt) → load nullopt");
  expect(!std::filesystem::exists(path, ec), "save(nullopt) removes the file");

  std::filesystem::remove(path, ec);
}

} // namespace

int main() {
  testRoundTripAndClear();
  if (failures == 0) {
    std::println("All SavedGame tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} SavedGame test(s) failed.", failures);
  return 1;
}
