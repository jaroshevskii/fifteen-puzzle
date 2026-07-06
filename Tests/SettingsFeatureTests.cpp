// Tests SettingsFeature: each action mutates the Shared settings value and
// persists it. A cell-backed strategy lets the test observe the persisted save.

import std;
import ComposableArchitecture;
import SettingsFeature;
import Sharing;
import AppSettings;

using ComposableArchitecture::TestStore;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

void testTogglesMutateAndPersist() {
  // A persistence strategy backed by a cell we can inspect.
  auto cell = std::make_shared<std::optional<AppSettings::Settings>>(AppSettings::Settings{});
  Sharing::PersistenceStrategy<AppSettings::Settings> strategy{
      .load = [cell] { return *cell; },
      .save = [cell](const AppSettings::Settings &value) { *cell = value; }};
  Sharing::Shared<AppSettings::Settings> shared(strategy);

  TestStore<SettingsFeature::State, SettingsFeature::Action> store(
      SettingsFeature::initialState(shared), SettingsFeature::body);

  store.send(SettingsFeature::SoundToggled{}, [](SettingsFeature::State &s) {
    s.settings.withMutation([](AppSettings::Settings &set) { set.isSoundEnabled = true; });
  });
  store.send(SettingsFeature::AutoResumeToggled{}, [](SettingsFeature::State &s) {
    s.settings.withMutation([](AppSettings::Settings &set) { set.autoResume = true; });
  });
  store.send(SettingsFeature::BoardSizeSelected{6}, [](SettingsFeature::State &s) {
    s.settings.withMutation([](AppSettings::Settings &set) { set.lastBoardSize = 6; });
  });
  store.send(SettingsFeature::PlayerNameChanged{"Ada"}, [](SettingsFeature::State &s) {
    s.settings.withMutation([](AppSettings::Settings &set) { set.playerName = "Ada"; });
  });

  expect(!store.failed(), "settings: state transitions match");
  expect(cell->has_value(), "settings: persisted at least once");
  if (cell->has_value()) {
    expect((*cell)->isSoundEnabled, "settings: sound persisted");
    expect((*cell)->autoResume, "settings: auto-resume persisted");
    expect((*cell)->lastBoardSize == 6, "settings: board size persisted");
    expect((*cell)->playerName == "Ada", "settings: player name persisted");
  }
}

void testBoardSizeClamped() {
  auto cell = std::make_shared<std::optional<AppSettings::Settings>>(AppSettings::Settings{});
  Sharing::PersistenceStrategy<AppSettings::Settings> strategy{
      .load = [cell] { return *cell; },
      .save = [cell](const AppSettings::Settings &value) { *cell = value; }};
  Sharing::Shared<AppSettings::Settings> shared(strategy);

  TestStore<SettingsFeature::State, SettingsFeature::Action> store(
      SettingsFeature::initialState(shared), SettingsFeature::body);

  store.send(SettingsFeature::BoardSizeSelected{99}, [](SettingsFeature::State &s) {
    s.settings.withMutation([](AppSettings::Settings &set) { set.lastBoardSize = 13; });
  });

  expect(!store.failed(), "settings: out-of-range board size is clamped to 13");
}

} // namespace

int main() {
  testTogglesMutateAndPersist();
  testBoardSizeClamped();
  if (failures == 0) {
    std::println("All SettingsFeature tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} SettingsFeature test(s) failed.", failures);
  return 1;
}
