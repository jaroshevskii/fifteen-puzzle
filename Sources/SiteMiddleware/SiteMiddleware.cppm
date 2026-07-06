export module SiteMiddleware;

import std;
import DatabaseClient;
import PuzzleCore;
import ServerRouter;
import SharedModels;

// The server's request handler — the C++ port of isowords' `SiteMiddleware`.
// It is pure "Request in, Response out" against an explicit `Environment`
// (no sockets, no globals), so integration tests can back the client's
// ApiClient with this exact middleware in-process, the same trick isowords
// uses to test client features against the real server logic.
//
// The server reuses the client-side `DatabaseClient` interface for its own
// SQLite storage, mirroring how isowords' server has its own DatabaseClient —
// the interface is identical, only the database file differs.
export namespace SiteMiddleware {

struct Environment {
  DatabaseClient::Client database;
};

// Server-side re-validation of a submitted score (the scaled-down analog of
// isowords replaying submitted moves): the server never trusts the client's
// numbers blindly. Returns a human-readable reason when the submission is
// rejected, nullopt when it is acceptable.
inline std::optional<std::string> validateSubmission(const SharedModels::ScoreSubmission &s) {
  const bool nameIsBlank =
      std::ranges::all_of(s.name, [](unsigned char c) { return std::isspace(c) != 0; });
  if (s.name.empty() || nameIsBlank) {
    return "name must not be empty";
  }
  if (s.name.size() > 32) {
    return "name must be at most 32 characters";
  }
  if (s.gridSize < PuzzleCore::minGrid || s.gridSize > PuzzleCore::maxGrid) {
    return std::format("gridSize must be between {} and {}", PuzzleCore::minGrid,
                       PuzzleCore::maxGrid);
  }
  if (s.moves < 1 || s.moves > 1'000'000) {
    return "moves out of range";
  }
  if (s.duration < 0 || s.duration > 24 * 60 * 60) {
    return "duration out of range";
  }
  return std::nullopt;
}

inline ServerRouter::Response respond(const Environment &environment,
                                      const ServerRouter::Request &request) {
  const auto route = ServerRouter::match(request);
  if (!route.has_value()) {
    // Distinguish a known route with a bad body from an unknown route: POST
    // /scores that fails to decode is the client's error, not a missing page.
    if (request.method == "POST" && request.path == "/scores") {
      return ServerRouter::Response{.status = 400, .body = R"({"error":"malformed submission"})"};
    }
    return ServerRouter::Response{.status = 404, .body = "{}"};
  }

  return std::visit(
      [&](auto &&value) -> ServerRouter::Response {
        using V = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<V, ServerRouter::FetchLeaderboard>) {
          if (value.gridSize < PuzzleCore::minGrid || value.gridSize > PuzzleCore::maxGrid) {
            return ServerRouter::Response{.status = 400,
                                          .body = R"({"error":"unsupported board size"})"};
          }
          auto entries = environment.database.fetchBestScores(value.gridSize);
          if (!entries.has_value()) {
            return ServerRouter::Response{.status = 500, .body = R"({"error":"database error"})"};
          }
          return ServerRouter::Response{.status = 200,
                                        .body = ServerRouter::encodeLeaderboardEntries(*entries)};

        } else if constexpr (std::is_same_v<V, ServerRouter::SubmitScore>) {
          if (const auto reason = validateSubmission(value.submission)) {
            return ServerRouter::Response{.status = 400,
                                          .body = std::format(R"({{"error":"{}"}})", *reason)};
          }
          if (!environment.database.saveGame(value.submission).has_value()) {
            return ServerRouter::Response{.status = 500, .body = R"({"error":"database error"})"};
          }
          return ServerRouter::Response{.status = 201, .body = "{}"};
        }
      },
      *route);
}

} // namespace SiteMiddleware
