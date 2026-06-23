// Tests the Sharing library: the in-memory strategy round-trips a value, and
// the JSON file strategy persists across reloads while treating a missing or
// corrupt file as "absent" (falling back to defaults rather than throwing).

import std;
import Sharing;
import AppSettings;
import AppSettingsLive;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

void testInMemoryRoundTrip() {
  auto strategy = Sharing::inMemory<AppSettings::Settings>();
  Sharing::Shared<AppSettings::Settings> shared(strategy);
  expect(shared.get().playerName == "Player", "inMemory: default loaded");

  shared.withMutation([](AppSettings::Settings &s) {
    s.playerName = "Ada";
    s.lastBoardSize = 6;
  });
  strategy.save(shared.get());

  // A second Shared built from the same strategy observes the save.
  Sharing::Shared<AppSettings::Settings> reloaded(strategy);
  expect(reloaded.get().playerName == "Ada", "inMemory: saved value reloads");
  expect(reloaded.get().lastBoardSize == 6, "inMemory: saved field reloads");
}

void testFileStorageRoundTrip() {
  const auto path = std::filesystem::temp_directory_path() / "fifteen-sharing-test.json";
  std::error_code ec;
  std::filesystem::remove(path, ec);

  // Missing file → default.
  {
    auto strategy = AppSettings::settingsFileStorage(path);
    Sharing::Shared<AppSettings::Settings> shared(strategy);
    expect(shared.get() == AppSettings::Settings{}, "fileStorage: missing file → default");
    shared.withMutation([](AppSettings::Settings &s) {
      s.isSoundEnabled = true;
      s.playerName = "Grace";
    });
    strategy.save(shared.get());
  }

  // Reload from disk.
  {
    auto strategy = AppSettings::settingsFileStorage(path);
    Sharing::Shared<AppSettings::Settings> shared(strategy);
    expect(shared.get().isSoundEnabled, "fileStorage: bool persisted");
    expect(shared.get().playerName == "Grace", "fileStorage: string persisted");
  }

  // Corrupt file → default.
  {
    std::ofstream out(path, std::ios::trunc);
    out << "{ this is not valid json";
    out.close();
    auto strategy = AppSettings::settingsFileStorage(path);
    Sharing::Shared<AppSettings::Settings> shared(strategy);
    expect(shared.get() == AppSettings::Settings{}, "fileStorage: corrupt file → default");
  }

  std::filesystem::remove(path, ec);
}

} // namespace

int main() {
  testInMemoryRoundTrip();
  testFileStorageRoundTrip();
  if (failures == 0) {
    std::println("All Sharing tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} Sharing test(s) failed.", failures);
  return 1;
}
