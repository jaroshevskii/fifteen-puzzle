export module LeaderboardFeature;

import std;
import ComposableArchitecture;
import ApiClient;
import SharedModels;

// A feature that combines the local (database) and remote (network)
// leaderboards — the C++ port of isowords' LeaderboardFeature. On
// appear/refresh it loads both sources concurrently and merges them; if the
// remote is unreachable it surfaces an offline hint while still showing the
// local scores. Winning a game routes a `ScoreSubmitted` here (from
// AppFeature), which persists locally and pushes to the server best-effort.
export namespace LeaderboardFeature {

struct State {
  int gridSize = 4;
  bool isVisible = false;
  bool isLoadingLocal = false;
  bool isLoadingRemote = false;
  std::vector<SharedModels::LeaderboardEntry> localEntries;
  std::vector<SharedModels::LeaderboardEntry> remoteEntries;
  // Set when the remote fetch fails; the view shows "offline" but still renders
  // `merged` (which then contains the local scores only).
  std::optional<ApiClient::ApiError> remoteError;
  std::vector<SharedModels::LeaderboardEntry> merged;

  bool operator==(const State &) const = default;
};

struct Appeared {};
struct Refreshed {};
struct VisibilityToggled {};
struct LocalLoaded {
  std::vector<SharedModels::LeaderboardEntry> entries;
};
struct RemoteResponse {
  std::expected<std::vector<SharedModels::LeaderboardEntry>,
                ApiClient::ApiError>
      result;
};
struct ScoreSubmitted {
  SharedModels::ScoreSubmission submission;
};
struct SubmitResponse {
  std::expected<void, ApiClient::ApiError> result;
};

using Action = std::variant<Appeared, Refreshed, VisibilityToggled, LocalLoaded,
                            RemoteResponse, ScoreSubmitted, SubmitResponse>;

State initialState();
ComposableArchitecture::Feature<State, Action> body();

// Pure merge of local + remote: sort by (duration, moves), drop duplicate
// (name, duration, moves) rows shared by both sources, cap to `limit`. Exposed
// for the view and tests.
std::vector<SharedModels::LeaderboardEntry>
mergeEntries(const std::vector<SharedModels::LeaderboardEntry> &local,
             const std::vector<SharedModels::LeaderboardEntry> &remote,
             int limit = 10);

} // namespace LeaderboardFeature
