// Tests LeaderboardFeature with the TestStore and stubbed clients. The store's
// onMount loads local + remote concurrently (run inline here), so each test
// receives a LocalLoaded then a RemoteResponse. Covers: merging local + remote,
// the offline fallback (remote fails → local-only), and the submit-on-win path
// persisting to the database.

import std;
import ComposableArchitecture;
import LeaderboardFeature;
import ApiClient;
import DatabaseClient;
import SharedModels;

using ComposableArchitecture::TestStore;
using Dependencies::DependencyContext;
using Dependencies::DependencyValues;
using Dependencies::withDependencies;
using SharedModels::LeaderboardEntry;
using SharedModels::ScoreSubmission;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

DatabaseClient::Client stubDatabase(std::vector<LeaderboardEntry> local) {
  DatabaseClient::Client client;
  client.fetchBestScores = [local](int) {
    return std::expected<std::vector<LeaderboardEntry>,
                         DatabaseClient::DbError>{local};
  };
  return client;
}

ApiClient::Client stubApi(std::vector<LeaderboardEntry> remote, bool offline) {
  ApiClient::Client client;
  client.fetchLeaderboard = [remote, offline](int, std::stop_token)
      -> std::expected<std::vector<LeaderboardEntry>, ApiClient::ApiError> {
    if (offline) {
      return std::unexpected(ApiClient::ApiError::offline);
    }
    return remote;
  };
  return client;
}

void testMergesLocalAndRemote() {
  const std::vector<LeaderboardEntry> local{
      {.name = "Ada", .gridSize = 4, .moves = 80, .duration = 42}};
  const std::vector<LeaderboardEntry> remote{
      {.name = "Bob", .gridSize = 4, .moves = 60, .duration = 30}};

  withDependencies(
      [&](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<DatabaseClient::Key>(stubDatabase(local));
        values.set<ApiClient::Key>(stubApi(remote, /*offline=*/false));
      },
      [&] {
        TestStore<LeaderboardFeature::State, LeaderboardFeature::Action> store(
            LeaderboardFeature::initialState(), LeaderboardFeature::body);

        store.receive([&](LeaderboardFeature::State &state) {
          state.isLoadingLocal = false;
          state.localEntries = local;
          state.merged = LeaderboardFeature::mergeEntries(local, {});
        });
        store.receive([&](LeaderboardFeature::State &state) {
          state.isLoadingRemote = false;
          state.remoteEntries = remote;
          state.merged = LeaderboardFeature::mergeEntries(local, remote);
        });

        expect(!store.failed(), "merge: state transitions match");
        expect(store.state().merged.size() == 2, "merge: both entries present");
        expect(!store.state().merged.empty() &&
                   store.state().merged.front().name == "Bob",
               "merge: fastest (Bob, 30s) ranked first");
        return 0;
      });
}

void testOfflineFallsBackToLocal() {
  const std::vector<LeaderboardEntry> local{
      {.name = "Ada", .gridSize = 4, .moves = 80, .duration = 42},
      {.name = "Cy", .gridSize = 4, .moves = 70, .duration = 35}};

  withDependencies(
      [&](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<DatabaseClient::Key>(stubDatabase(local));
        values.set<ApiClient::Key>(stubApi({}, /*offline=*/true));
      },
      [&] {
        TestStore<LeaderboardFeature::State, LeaderboardFeature::Action> store(
            LeaderboardFeature::initialState(), LeaderboardFeature::body);

        store.receive([&](LeaderboardFeature::State &state) {
          state.isLoadingLocal = false;
          state.localEntries = local;
          state.merged = LeaderboardFeature::mergeEntries(local, {});
        });
        store.receive([&](LeaderboardFeature::State &state) {
          state.isLoadingRemote = false;
          state.remoteError = ApiClient::ApiError::offline;
          state.merged = LeaderboardFeature::mergeEntries(local, {});
        });

        expect(!store.failed(), "offline: state transitions match");
        expect(store.state().remoteError == ApiClient::ApiError::offline,
               "offline: error surfaced");
        expect(store.state().merged.size() == 2,
               "offline: still shows the two local scores");
        return 0;
      });
}

void testScoreSubmittedSavesLocally() {
  auto recorder = std::make_shared<std::vector<ScoreSubmission>>();
  const ScoreSubmission submission{
      .name = "Ada", .gridSize = 4, .moves = 50, .duration = 25};

  withDependencies(
      [&](DependencyValues &values) {
        values.context = DependencyContext::test;
        DatabaseClient::Client db = stubDatabase({});
        db.saveGame = [recorder](ScoreSubmission s) {
          recorder->push_back(std::move(s));
          return std::expected<void, DatabaseClient::DbError>{};
        };
        values.set<DatabaseClient::Key>(db);
        values.set<ApiClient::Key>(stubApi({}, /*offline=*/false));
      },
      [&] {
        TestStore<LeaderboardFeature::State, LeaderboardFeature::Action> store(
            LeaderboardFeature::initialState(), LeaderboardFeature::body);

        // Drain onMount's initial load (both empty).
        store.receive([](LeaderboardFeature::State &state) {
          state.isLoadingLocal = false;
        });
        store.receive([](LeaderboardFeature::State &state) {
          state.isLoadingRemote = false;
        });

        // Submitting persists locally (no direct state change) then refreshes.
        store.send(LeaderboardFeature::ScoreSubmitted{submission}, {});
        store.receive([](LeaderboardFeature::State &state) {
          state.isLoadingLocal = true;
          state.isLoadingRemote = true;
        });                // Refreshed
        store.receive({}); // SubmitResponse (success → no change)
        store.receive([](LeaderboardFeature::State &state) {
          state.isLoadingLocal = false;
        }); // LocalLoaded
        store.receive([](LeaderboardFeature::State &state) {
          state.isLoadingRemote = false;
        }); // RemoteResponse

        expect(!store.failed(), "submit: state transitions match");
        expect(recorder->size() == 1, "submit: saveGame called once");
        expect(!recorder->empty() && recorder->front() == submission,
               "submit: saved the submitted score");
        return 0;
      });
}

} // namespace

int main() {
  testMergesLocalAndRemote();
  testOfflineFallsBackToLocal();
  testScoreSubmittedSavesLocally();

  if (failures == 0) {
    std::println("All LeaderboardFeature tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} LeaderboardFeature test(s) failed.", failures);
  return 1;
}
