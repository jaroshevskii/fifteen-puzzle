export module DatabaseClient;

import std;
import Dependencies;
import SharedModels;

// Interface for the local persistence dependency — a port of isowords'
// `LocalDatabaseClient`. It records completed games and answers leaderboard /
// stats queries. The live (SQLite) implementation lives in
// `DatabaseClientLive`; tests inject an in-memory stub. Operations return
// `std::expected` so a failed query degrades gracefully (the feature falls back
// to showing what it has) rather than throwing across the async boundary.
export namespace DatabaseClient {

enum class DbError : std::uint8_t {
  openFailed,  // the database could not be opened
  queryFailed, // a statement failed (migration, insert, or select)
};

struct Client {
  // Creates the schema if needed (idempotent).
  std::function<std::expected<void, DbError>()> migrate = [] {
    return std::expected<void, DbError>{};
  };
  // Persists a completed game.
  std::function<std::expected<void, DbError>(SharedModels::ScoreSubmission)>
      saveGame = [](SharedModels::ScoreSubmission) {
        return std::expected<void, DbError>{};
      };
  // The local top-N scores for a board size, fastest first.
  std::function<
      std::expected<std::vector<SharedModels::LeaderboardEntry>, DbError>(int)>
      fetchBestScores = [](int) {
        return std::expected<std::vector<SharedModels::LeaderboardEntry>,
                             DbError>{std::in_place};
      };
  // Aggregate stats across all recorded games.
  std::function<std::expected<SharedModels::Stats, DbError>()> fetchStats = [] {
    return std::expected<SharedModels::Stats, DbError>{std::in_place};
  };
};

struct Key : Dependencies::DependencyKey<Key, Client> {
  static Client liveValue() {
    return Client{};
  } // real implementation supplied by DatabaseClientLive
  static Client testValue() { return Client{}; } // tests inject a stub
};

} // namespace DatabaseClient
