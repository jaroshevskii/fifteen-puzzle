export module DatabaseClientLive;

import std;
import DatabaseClient;
import SharedModels;
import Sqlite;

// Live, SQLite-backed implementation of the database dependency. Wire it in at
// app launch:
//
//   prepareDependencies([](DependencyValues& values) {
//     values.set<DatabaseClient::Key>(DatabaseClient::live(dbPath));
//   });
export namespace DatabaseClient {

// Returns a client backed by the database at `dbPath` (use ":memory:" for an
// ephemeral database in tests). The single connection is shared by all of the
// client's closures and serialized with a mutex, so it is safe to call from the
// store's background tasks. If the database can't be opened, every operation
// returns DbError::openFailed rather than throwing.
Client live(std::string dbPath);

} // namespace DatabaseClient

namespace DatabaseClient {

namespace {

// One shared connection guarded by a mutex (SQLite connections are not meant to
// be used concurrently from multiple threads).
struct Connection {
  std::mutex mutex;
  std::optional<Sqlite::Database> database;
};

SharedModels::LeaderboardEntry entryFromRow(const Sqlite::Row &row) {
  return SharedModels::LeaderboardEntry{
      .name = std::get<std::string>(row[0]),
      .gridSize = static_cast<int>(std::get<std::int64_t>(row[1])),
      .moves = static_cast<int>(std::get<std::int64_t>(row[2])),
      .duration = static_cast<int>(std::get<std::int64_t>(row[3])),
      .playedAt = std::get<double>(row[4])};
}

} // namespace

Client live(std::string dbPath) {
  auto connection = std::make_shared<Connection>();
  try {
    connection->database.emplace(dbPath);
  } catch (...) {
    // Leave the connection empty; every op below reports openFailed.
  }

  return Client{
      .migrate = [connection]() -> std::expected<void, DbError> {
        std::scoped_lock lock(connection->mutex);
        if (!connection->database) {
          return std::unexpected(DbError::openFailed);
        }
        try {
          auto &db = *connection->database;
          const auto rows = db.run("PRAGMA user_version");
          const std::int64_t version = rows.empty() ? 0 : std::get<std::int64_t>(rows[0][0]);
          if (version < 1) {
            db.execute("CREATE TABLE IF NOT EXISTS games ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "name TEXT NOT NULL,"
                       "grid_size INTEGER NOT NULL,"
                       "moves INTEGER NOT NULL,"
                       "duration_seconds INTEGER NOT NULL,"
                       "played_at REAL NOT NULL)");
            db.execute("CREATE INDEX IF NOT EXISTS idx_games_grid_dur "
                       "ON games(grid_size, duration_seconds)");
            db.execute("PRAGMA user_version = 1");
          }
          return {};
        } catch (...) {
          return std::unexpected(DbError::queryFailed);
        }
      },
      .saveGame = [connection](SharedModels::ScoreSubmission s) -> std::expected<void, DbError> {
        std::scoped_lock lock(connection->mutex);
        if (!connection->database) {
          return std::unexpected(DbError::openFailed);
        }
        try {
          connection->database->run("INSERT INTO games (name, grid_size, moves, duration_seconds, "
                                    "played_at) VALUES (?, ?, ?, ?, ?)",
                                    {Sqlite::Datatype{s.name},
                                     Sqlite::Datatype{static_cast<std::int64_t>(s.gridSize)},
                                     Sqlite::Datatype{static_cast<std::int64_t>(s.moves)},
                                     Sqlite::Datatype{static_cast<std::int64_t>(s.duration)},
                                     Sqlite::Datatype{s.playedAt}});
          return {};
        } catch (...) {
          return std::unexpected(DbError::queryFailed);
        }
      },
      .fetchBestScores = [connection](int gridSize)
          -> std::expected<std::vector<SharedModels::LeaderboardEntry>, DbError> {
        std::scoped_lock lock(connection->mutex);
        if (!connection->database) {
          return std::unexpected(DbError::openFailed);
        }
        try {
          const auto rows = connection->database->run(
              "SELECT name, grid_size, moves, duration_seconds, played_at "
              "FROM games WHERE grid_size = ? "
              "ORDER BY duration_seconds ASC, moves ASC LIMIT 10",
              {Sqlite::Datatype{static_cast<std::int64_t>(gridSize)}});
          std::vector<SharedModels::LeaderboardEntry> entries;
          entries.reserve(rows.size());
          for (const auto &row : rows) {
            entries.push_back(entryFromRow(row));
          }
          return entries;
        } catch (...) {
          return std::unexpected(DbError::queryFailed);
        }
      },
      .fetchStats = [connection]() -> std::expected<SharedModels::Stats, DbError> {
        std::scoped_lock lock(connection->mutex);
        if (!connection->database) {
          return std::unexpected(DbError::openFailed);
        }
        try {
          const auto rows =
              connection->database->run("SELECT COUNT(*), COALESCE(MIN(duration_seconds), 0), "
                                        "COALESCE(SUM(duration_seconds), 0) FROM games");
          SharedModels::Stats stats;
          if (!rows.empty()) {
            stats.gamesPlayed = static_cast<int>(std::get<std::int64_t>(rows[0][0]));
            stats.bestDurationSeconds = static_cast<int>(std::get<std::int64_t>(rows[0][1]));
            stats.totalSeconds = static_cast<double>(std::get<std::int64_t>(rows[0][2]));
          }
          return stats;
        } catch (...) {
          return std::unexpected(DbError::queryFailed);
        }
      }};
}

} // namespace DatabaseClient
