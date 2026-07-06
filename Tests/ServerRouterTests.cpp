// Tests the shared ServerRouter: every route must survive a print → match
// round trip (the property that keeps client and server in agreement), and the
// JSON codecs must round-trip the shared models.

import std;
import ServerRouter;
import SharedModels;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

const SharedModels::ScoreSubmission kSubmission{
    .name = "Ada", .gridSize = 5, .moves = 240, .duration = 96, .playedAt = 1234.5};

void testFetchLeaderboardRoundTrip() {
  const ServerRouter::Route route = ServerRouter::FetchLeaderboard{.gridSize = 7};
  const ServerRouter::Request request = ServerRouter::print(route);

  expect(request.method == "GET", "fetch: prints as GET");
  expect(request.path == "/leaderboard", "fetch: prints the /leaderboard path");
  expect(request.query == "size=7", "fetch: prints the size query");

  const auto matched = ServerRouter::match(request);
  expect(matched.has_value() && matched == route, "fetch: match(print(route)) == route");
}

void testSubmitScoreRoundTrip() {
  const ServerRouter::Route route = ServerRouter::SubmitScore{.submission = kSubmission};
  const ServerRouter::Request request = ServerRouter::print(route);

  expect(request.method == "POST", "submit: prints as POST");
  expect(request.path == "/scores", "submit: prints the /scores path");

  const auto matched = ServerRouter::match(request);
  expect(matched.has_value() && matched == route, "submit: match(print(route)) == route");
}

void testUnknownRoutesDoNotMatch() {
  expect(!ServerRouter::match({.method = "GET", .path = "/nope"}).has_value(),
         "unknown path does not match");
  expect(!ServerRouter::match({.method = "DELETE", .path = "/leaderboard"}).has_value(),
         "wrong method does not match");
  expect(!ServerRouter::match({.method = "GET", .path = "/leaderboard", .query = "size=abc"})
              .has_value(),
         "non-numeric size does not match");
  expect(
      !ServerRouter::match({.method = "POST", .path = "/scores", .body = "not json"}).has_value(),
      "malformed submission body does not match");
}

void testCodecsRoundTrip() {
  const auto decodedSubmission =
      ServerRouter::decodeScoreSubmission(ServerRouter::encodeScoreSubmission(kSubmission));
  expect(decodedSubmission.has_value() && *decodedSubmission == kSubmission,
         "submission codec round-trips");

  const std::vector<SharedModels::LeaderboardEntry> entries{
      {.name = "Ada", .gridSize = 4, .moves = 80, .duration = 42, .playedAt = 1.0},
      {.name = "Bob", .gridSize = 4, .moves = 60, .duration = 30, .playedAt = 2.0}};
  const auto decodedEntries =
      ServerRouter::decodeLeaderboardEntries(ServerRouter::encodeLeaderboardEntries(entries));
  expect(decodedEntries.has_value() && *decodedEntries == entries, "leaderboard codec round-trips");

  expect(!ServerRouter::decodeLeaderboardEntries("{\"not\":\"an array\"}").has_value() ||
             ServerRouter::decodeLeaderboardEntries("{\"not\":\"an array\"}")->empty(),
         "non-array leaderboard body decodes to nothing useful");
  expect(!ServerRouter::decodeScoreSubmission("[]").has_value(),
         "wrong-shape submission fails to decode");
}

} // namespace

int main() {
  testFetchLeaderboardRoundTrip();
  testSubmitScoreRoundTrip();
  testUnknownRoutesDoNotMatch();
  testCodecsRoundTrip();

  if (failures == 0) {
    std::println("All ServerRouter tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} ServerRouter test(s) failed.", failures);
  return 1;
}
