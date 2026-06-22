#include <raylib.h>

import std;
import ComposableArchitecture;
import Dependencies;
import AudioPlayerClient;
import AudioPlayerClientLive;
import SolverClient;
import SolverClientLive;
import ApiClient;
import ApiClientLive;
import DatabaseClient;
import DatabaseClientLive;
import Sharing;
import AppSettings;
import AppSettingsLive;
import AppFeature;
import AppFeatureView;
import PuzzleFeature;

using ComposableArchitecture::RootStore;
using Dependencies::DependencyValues;
using Dependencies::prepareDependencies;

namespace {

// Per-user data directory for the settings file and the SQLite database.
std::filesystem::path appDataDir() {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  if (const char *appdata = std::getenv("APPDATA"); appdata && *appdata) {
    return fs::path(appdata) / "FifteenPuzzle";
  }
#elif defined(__APPLE__)
  if (const char *home = std::getenv("HOME"); home && *home) {
    return fs::path(home) / "Library" / "Application Support" / "FifteenPuzzle";
  }
#else
  if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
    return fs::path(xdg) / "FifteenPuzzle";
  }
  if (const char *home = std::getenv("HOME"); home && *home) {
    return fs::path(home) / ".local" / "share" / "FifteenPuzzle";
  }
#endif
  return fs::current_path();
}

} // namespace

int main() {
  const std::filesystem::path dataDir = appDataDir();
  std::error_code ec;
  std::filesystem::create_directories(dataDir, ec);

  // Persisted settings load now (before the store), so onMount sees them.
  Sharing::Shared<AppSettings::Settings> settings(
      AppSettings::settingsFileStorage(dataDir / "settings.json"));

  // Wire up live dependencies as early as possible, the analog of TCA's
  // `prepareDependencies` at the app entry point.
  prepareDependencies([&](DependencyValues &values) {
    values.set<AudioPlayerClient::Key>(AudioPlayerClient::live());
    values.set<SolverClient::Key>(SolverClient::live());
    // Reads FIFTEEN_API_BASE_URL; unreachable → calls degrade to offline.
    values.set<ApiClient::Key>(ApiClient::live());

    auto database = DatabaseClient::live((dataDir / "games.sqlite3").string());
    (void)database.migrate(); // ensure the schema exists before any query
    values.set<DatabaseClient::Key>(database);

    // Resolve the remaining keys now so the dependency storage is fully
    // populated before any background effect thread reads from it.
    (void)values.get<Dependencies::DateGeneratorKey>();
    (void)values.get<Dependencies::RandomNumberGeneratorKey>();
    (void)values.get<ApiClient::Key>();
    (void)values.get<DatabaseClient::Key>();
  });

  namespace Config = PuzzleFeature::Config;
  SetConfigFlags(FLAG_VSYNC_HINT);
  InitWindow(Config::windowWidth(Config::minGrid),
             Config::windowHeight(Config::minGrid), "N Puzzle");

  // Constructing the store runs the feature's onMount (first shuffle + timer,
  // and the leaderboard's initial load), so there is no AppLaunched action.
  RootStore<AppFeature::State, AppFeature::Action> store(
      AppFeature::initialState(std::move(settings)), AppFeature::body);

  int displayedGrid = Config::minGrid;

  while (!WindowShouldClose()) {
    for (const auto &action : AppFeatureView::collectActions(store.state())) {
      store.send(action);
    }

    // Deliver any actions produced by background effects (solver, network, db).
    store.pump();

    // Resize the window when the board size changes.
    const int grid = store.state().puzzle.grid;
    if (grid != displayedGrid) {
      SetWindowSize(Config::windowWidth(grid), Config::windowHeight(grid));
      displayedGrid = grid;
    }

    BeginDrawing();
    ClearBackground(BLACK);
    AppFeatureView::draw(store.state());
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
