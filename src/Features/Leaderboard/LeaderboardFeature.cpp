module LeaderboardFeature; // implementation unit

import std;
import ComposableArchitecture;
import ApiClient;
import DatabaseClient;
import SharedModels;

namespace LeaderboardFeature {

namespace {
// The remote fetch is cancellable so a rapid refresh supersedes the prior one.
constexpr std::string_view kRemoteCancelId = "leaderboard-remote";
} // namespace

State initialState() { return State{}; }

std::vector<SharedModels::LeaderboardEntry>
mergeEntries(const std::vector<SharedModels::LeaderboardEntry> &local,
             const std::vector<SharedModels::LeaderboardEntry> &remote, int limit) {
  std::vector<SharedModels::LeaderboardEntry> all;
  all.reserve(local.size() + remote.size());
  all.insert(all.end(), local.begin(), local.end());
  all.insert(all.end(), remote.begin(), remote.end());
  std::ranges::sort(
      all, [](const SharedModels::LeaderboardEntry &a, const SharedModels::LeaderboardEntry &b) {
        return std::tie(a.duration, a.moves) < std::tie(b.duration, b.moves);
      });
  const auto dup = std::ranges::unique(
      all, [](const SharedModels::LeaderboardEntry &a, const SharedModels::LeaderboardEntry &b) {
        return a.name == b.name && a.duration == b.duration && a.moves == b.moves;
      });
  all.erase(dup.begin(), dup.end());
  if (limit >= 0 && static_cast<int>(all.size()) > limit) {
    all.resize(static_cast<std::size_t>(limit));
  }
  return all;
}

ComposableArchitecture::Feature<State, Action> body() {
  using FeatureStore = ComposableArchitecture::Store<State, Action>;

  // Kick off both loads (local + remote) for the current board size.
  const auto load = [](State &state, FeatureStore &store) {
    state.isLoadingLocal = true;
    state.isLoadingRemote = true;
    state.remoteError = std::nullopt;
    const int gridSize = state.gridSize;

    store.addTask([gridSize](FeatureStore &store, std::stop_token) {
      Dependencies::Dependency<DatabaseClient::Key> db;
      auto rows = db->fetchBestScores(gridSize);
      store.send(LocalLoaded{rows.has_value() ? std::move(*rows)
                                              : std::vector<SharedModels::LeaderboardEntry>{}});
    });

    store.addTask(
        [gridSize](FeatureStore &store, std::stop_token stop) {
          Dependencies::Dependency<ApiClient::Key> api;
          store.send(RemoteResponse{api->fetchLeaderboard(gridSize, std::move(stop))});
        },
        std::string(kRemoteCancelId));
  };

  return ComposableArchitecture::Update<State, Action>(
             [load](State &state, const Action &action, FeatureStore &store) {
               std::visit(
                   [&](auto &&value) {
                     using V = std::decay_t<decltype(value)>;

                     if constexpr (std::is_same_v<V, Appeared> || std::is_same_v<V, Refreshed>) {
                       load(state, store);
                     } else if constexpr (std::is_same_v<V, VisibilityToggled>) {
                       state.isVisible = !state.isVisible;
                       if (state.isVisible) {
                         load(state, store); // refresh whenever opened
                       }
                     } else if constexpr (std::is_same_v<V, LocalLoaded>) {
                       state.isLoadingLocal = false;
                       state.localEntries = value.entries;
                       state.merged = mergeEntries(state.localEntries, state.remoteEntries);
                     } else if constexpr (std::is_same_v<V, RemoteResponse>) {
                       state.isLoadingRemote = false;
                       if (value.result.has_value()) {
                         state.remoteEntries = *value.result;
                         state.remoteError = std::nullopt;
                       } else {
                         state.remoteEntries.clear();
                         state.remoteError = value.result.error();
                       }
                       state.merged = mergeEntries(state.localEntries, state.remoteEntries);
                     } else if constexpr (std::is_same_v<V, ScoreSubmitted>) {
                       const auto submission = value.submission;
                       // Persist locally (source of truth), then reload.
                       store.addTask([submission](FeatureStore &store, std::stop_token) {
                         Dependencies::Dependency<DatabaseClient::Key> db;
                         (void)db->saveGame(submission);
                         store.send(Refreshed{});
                       });
                       // Push to the server best-effort.
                       store.addTask([submission](FeatureStore &store, std::stop_token stop) {
                         Dependencies::Dependency<ApiClient::Key> api;
                         store.send(SubmitResponse{api->submitScore(submission, std::move(stop))});
                       });
                     } else if constexpr (std::is_same_v<V, SubmitResponse>) {
                       if (!value.result.has_value()) {
                         state.remoteError = value.result.error();
                       }
                     }
                   },
                   action);
             })
      .onMount([load](State &state, FeatureStore &store) {
        load(state, store); // load local + remote at launch
      });
}

} // namespace LeaderboardFeature
