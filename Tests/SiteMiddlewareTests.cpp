// Server integration tests, the isowords way: the middleware is pure
// Request → Response against an explicit environment, so we exercise the real
// route matching, validation and database logic in-process — and then back the
// client's ApiClient with the same middleware and drive a client feature
// (LeaderboardFeature) against the real server logic, no sockets anywhere.

import std;
import ApiClient;
import ComposableArchitecture;
import DatabaseClient;
import DatabaseClientLive;
import LeaderboardFeature;
import ServerRouter;
import SharedModels;
import SiteMiddleware;

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

SiteMiddleware::Environment inMemoryEnvironment() {
  auto database = DatabaseClient::live(":memory:");
  (void)database.migrate();
  return SiteMiddleware::Environment{.database = std::move(database)};
}

const ScoreSubmission kSubmission{
    .name = "Ada", .gridSize = 4, .moves = 120, .duration = 63, .playedAt = 100.0};

// An ApiClient whose closures call the middleware directly — the analog of
// isowords' `ApiClient.init(middleware:router:)` test helper. The client
// prints routes with the shared router; the middleware matches them back.
ApiClient::Client middlewareBackedClient(SiteMiddleware::Environment environment) {
  ApiClient::Client client;
  client.fetchLeaderboard = [environment](int gridSize, std::stop_token)
      -> std::expected<std::vector<LeaderboardEntry>, ApiClient::ApiError> {
    const auto response = SiteMiddleware::respond(
        environment, ServerRouter::print(ServerRouter::FetchLeaderboard{.gridSize = gridSize}));
    if (response.status != 200) {
      return std::unexpected(ApiClient::ApiError::httpError);
    }
    auto entries = ServerRouter::decodeLeaderboardEntries(response.body);
    if (!entries.has_value()) {
      return std::unexpected(ApiClient::ApiError::decodingError);
    }
    return std::move(*entries);
  };
  client.submitScore = [environment](ScoreSubmission submission,
                                     std::stop_token) -> std::expected<void, ApiClient::ApiError> {
    const auto response = SiteMiddleware::respond(
        environment, ServerRouter::print(ServerRouter::SubmitScore{.submission = submission}));
    if (response.status < 200 || response.status >= 300) {
      return std::unexpected(ApiClient::ApiError::httpError);
    }
    return {};
  };
  return client;
}

void testSubmitThenFetchRoundTrip() {
  const auto environment = inMemoryEnvironment();

  const auto submit = SiteMiddleware::respond(
      environment, ServerRouter::print(ServerRouter::SubmitScore{.submission = kSubmission}));
  expect(submit.status == 201, "submit: valid submission answers 201");

  const auto fetch = SiteMiddleware::respond(
      environment, ServerRouter::print(ServerRouter::FetchLeaderboard{.gridSize = 4}));
  expect(fetch.status == 200, "fetch: answers 200");
  const auto entries = ServerRouter::decodeLeaderboardEntries(fetch.body);
  expect(entries.has_value() && entries->size() == 1, "fetch: returns the submitted row");
  expect(entries.has_value() && !entries->empty() && entries->front().name == "Ada" &&
             entries->front().duration == 63,
         "fetch: row carries the submitted fields");

  const auto otherSize = SiteMiddleware::respond(
      environment, ServerRouter::print(ServerRouter::FetchLeaderboard{.gridSize = 5}));
  const auto otherEntries = ServerRouter::decodeLeaderboardEntries(otherSize.body);
  expect(otherEntries.has_value() && otherEntries->empty(),
         "fetch: other board sizes have their own leaderboard");
}

void testServerSideValidation() {
  const auto environment = inMemoryEnvironment();

  auto blankName = kSubmission;
  blankName.name = "   ";
  expect(SiteMiddleware::respond(
             environment, ServerRouter::print(ServerRouter::SubmitScore{.submission = blankName}))
                 .status == 400,
         "validation: blank name rejected");

  auto badGrid = kSubmission;
  badGrid.gridSize = 3;
  expect(SiteMiddleware::respond(
             environment, ServerRouter::print(ServerRouter::SubmitScore{.submission = badGrid}))
                 .status == 400,
         "validation: unsupported grid rejected");

  auto badDuration = kSubmission;
  badDuration.duration = -5;
  expect(SiteMiddleware::respond(
             environment, ServerRouter::print(ServerRouter::SubmitScore{.submission = badDuration}))
                 .status == 400,
         "validation: negative duration rejected");

  const auto fetch = SiteMiddleware::respond(
      environment, ServerRouter::print(ServerRouter::FetchLeaderboard{.gridSize = 4}));
  const auto entries = ServerRouter::decodeLeaderboardEntries(fetch.body);
  expect(entries.has_value() && entries->empty(), "validation: nothing rejected was stored");

  expect(SiteMiddleware::respond(environment, {.method = "GET", .path = "/nope"}).status == 404,
         "routing: unknown route answers 404");
  expect(SiteMiddleware::respond(environment,
                                 {.method = "POST", .path = "/scores", .body = "not json"})
                 .status == 400,
         "routing: malformed submission answers 400");
  expect(SiteMiddleware::respond(environment,
                                 {.method = "GET", .path = "/leaderboard", .query = "size=99"})
                 .status == 400,
         "routing: out-of-range board size answers 400");
}

// The full isowords integration pattern: a client feature runs against the
// real middleware + database through its normal ApiClient dependency.
void testLeaderboardFeatureAgainstRealMiddleware() {
  auto environment = inMemoryEnvironment();
  (void)environment.database.saveGame(kSubmission); // pre-existing server score

  auto localDatabase = DatabaseClient::live(":memory:");
  (void)localDatabase.migrate();

  withDependencies(
      [&](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<DatabaseClient::Key>(localDatabase);
        values.set<ApiClient::Key>(middlewareBackedClient(environment));
      },
      [&] {
        TestStore<LeaderboardFeature::State, LeaderboardFeature::Action> store(
            LeaderboardFeature::initialState(), LeaderboardFeature::body);

        const LeaderboardEntry serverRow{
            .name = "Ada", .gridSize = 4, .moves = 120, .duration = 63, .playedAt = 100.0};

        store.receive([&](LeaderboardFeature::State &state) {
          state.isLoadingLocal = false;
          state.merged = {};
        });
        store.receive([&](LeaderboardFeature::State &state) {
          state.isLoadingRemote = false;
          state.remoteEntries = {serverRow};
          state.merged = {serverRow};
        });

        expect(!store.failed(), "integration: state transitions match");
        expect(store.state().merged.size() == 1 && store.state().merged.front().name == "Ada",
               "integration: the feature shows the server's row");
        return 0;
      });
}

} // namespace

int main() {
  testSubmitThenFetchRoundTrip();
  testServerSideValidation();
  testLeaderboardFeatureAgainstRealMiddleware();

  if (failures == 0) {
    std::println("All SiteMiddleware tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} SiteMiddleware test(s) failed.", failures);
  return 1;
}
