export module ApiClient;

import std;
import Dependencies;
import SharedModels;

// Interface for the remote leaderboard service — a port of isowords'
// `ApiClient` (trimmed to this game's needs). The live (libcurl + JSON)
// implementation lives in `ApiClientLive`; tests inject a stub. Both calls
// honor a `std::stop_token` so a background task can cancel an in-flight
// request, and return `std::expected` so the feature can fall back to local
// data when offline.
export namespace ApiClient {

enum class ApiError : std::uint8_t {
  offline,       // transport failure (no network, DNS, connection, timeout)
  httpError,     // reached the server but it answered non-2xx
  decodingError, // 2xx, but the body did not parse / match the shape
  cancelled,     // the stop_token was tripped
};

struct Client {
  // The top scores for a board size, as the server ranks them.
  std::function<std::expected<std::vector<SharedModels::LeaderboardEntry>, ApiError>(
      int /*gridSize*/, std::stop_token)>
      fetchLeaderboard = [](int, std::stop_token) {
        return std::expected<std::vector<SharedModels::LeaderboardEntry>, ApiError>{std::in_place};
      };

  // Submits a completed game to the server.
  std::function<std::expected<void, ApiError>(SharedModels::ScoreSubmission, std::stop_token)>
      submitScore = [](SharedModels::ScoreSubmission, std::stop_token) {
        return std::expected<void, ApiError>{};
      };
};

struct Key : Dependencies::DependencyKey<Key, Client> {
  // Real implementation supplied by ApiClientLive.
  static Client liveValue() { return Client{}; }
  // Tests default to typed "offline" so a test that forgets to stub the client
  // never reaches the network and exercises the offline-fallback path.
  static Client testValue() {
    return Client{.fetchLeaderboard =
                      [](int, std::stop_token) {
                        return std::expected<std::vector<SharedModels::LeaderboardEntry>, ApiError>{
                            std::unexpected(ApiError::offline)};
                      },
                  .submitScore =
                      [](SharedModels::ScoreSubmission, std::stop_token) {
                        return std::expected<void, ApiError>{std::unexpected(ApiError::offline)};
                      }};
  }
};

} // namespace ApiClient
