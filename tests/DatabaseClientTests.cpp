// Tests the SQLite-backed database client against an in-memory (":memory:")
// database: migrate, save a few games, then verify best-scores ordering (per
// board size, fastest first) and the aggregate stats.

import std;
import DatabaseClient;
import DatabaseClientLive;
import SharedModels;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

void testSaveAndQuery() {
  auto db = DatabaseClient::live(":memory:");

  expect(db.migrate().has_value(), "migrate succeeds");

  db.saveGame({.name = "Ada", .gridSize = 4, .moves = 80, .duration = 42, .playedAt = 1000.0});
  db.saveGame({.name = "Bob", .gridSize = 4, .moves = 60, .duration = 30, .playedAt = 2000.0});
  db.saveGame({.name = "Cara", .gridSize = 5, .moves = 99, .duration = 99, .playedAt = 3000.0});

  const auto best4 = db.fetchBestScores(4);
  expect(best4.has_value(), "fetchBestScores(4) succeeds");
  if (best4.has_value()) {
    expect(best4->size() == 2, "two 4x4 scores recorded");
    expect(!best4->empty() && best4->front().duration == 30, "fastest 4x4 score is first");
    expect(!best4->empty() && best4->front().name == "Bob", "fastest 4x4 score is Bob's");
  }

  const auto best5 = db.fetchBestScores(5);
  expect(best5.has_value() && best5->size() == 1, "one 5x5 score recorded");

  const auto stats = db.fetchStats();
  expect(stats.has_value(), "fetchStats succeeds");
  if (stats.has_value()) {
    expect(stats->gamesPlayed == 3, "three games played");
    expect(stats->bestDurationSeconds == 30, "best duration overall is 30");
    expect(stats->totalSeconds == 171.0, "total seconds is 42+30+99");
  }
}

void testEmptyDatabase() {
  auto db = DatabaseClient::live(":memory:");
  expect(db.migrate().has_value(), "migrate on empty db");
  const auto best = db.fetchBestScores(4);
  expect(best.has_value() && best->empty(), "no scores yet → empty list");
  const auto stats = db.fetchStats();
  expect(stats.has_value() && stats->gamesPlayed == 0, "no games yet → 0");
}

} // namespace

int main() {
  testSaveAndQuery();
  testEmptyDatabase();
  if (failures == 0) {
    std::println("All DatabaseClient tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} DatabaseClient test(s) failed.", failures);
  return 1;
}
