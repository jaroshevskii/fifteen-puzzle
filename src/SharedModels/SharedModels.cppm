export module SharedModels;

import std;

// The plain value types shared across the network and database clients, so that
// neither client has to depend on the other (mirroring isowords' `SharedModels`
// target). They carry no behaviour — JSON glue for the wire lives in the live
// network module, and the database maps them to/from columns directly.
export namespace SharedModels {

// A row on a leaderboard, whether it came from the local database or the remote
// API. `playedAt` is in `DateGenerator::now()` seconds; `gridSize` keys the
// per-board leaderboards (the 4x4 board has a different board than the 13x13).
struct LeaderboardEntry {
  std::string name;
  int gridSize = 0;
  int moves = 0;
  int duration = 0; // seconds
  double playedAt = 0.0;

  bool operator==(const LeaderboardEntry &) const = default;
};

// A completed game to persist locally and/or submit remotely. Same fields as a
// leaderboard row — a submission becomes an entry once it is stored.
struct ScoreSubmission {
  std::string name;
  int gridSize = 0;
  int moves = 0;
  int duration = 0; // seconds
  double playedAt = 0.0;

  bool operator==(const ScoreSubmission &) const = default;
};

// Aggregate local stats, computed by the database client.
struct Stats {
  int gamesPlayed = 0;
  int bestDurationSeconds = 0;
  double totalSeconds = 0.0;

  bool operator==(const Stats &) const = default;
};

} // namespace SharedModels
