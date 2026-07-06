export module ServerRouter;

import std;
import SharedModels;

// The HTTP API surface, defined once and shared by both sides — the C++ port
// of isowords' `ServerRouter` ParserPrinter. The client (ApiClientLive) turns a
// `Route` into a transport-neutral `Request` with `print`; the server
// (SiteMiddleware) turns an incoming `Request` back into a `Route` with
// `match`. Because both directions come from the same definitions, the client
// and server can never disagree about paths, queries, or body shapes — and the
// integration tests back the client with the real middleware in-process.
//
// The JSON codecs live in the implementation unit so nlohmann/json stays out
// of the reachable interface (it would clash with `import std`).
export namespace ServerRouter {

// GET /leaderboard?size={gridSize} — the top scores for a board size.
struct FetchLeaderboard {
  int gridSize = 4;
  bool operator==(const FetchLeaderboard &) const = default;
};

// POST /scores — submit a completed game.
struct SubmitScore {
  SharedModels::ScoreSubmission submission;
  bool operator==(const SubmitScore &) const = default;
};

using Route = std::variant<FetchLeaderboard, SubmitScore>;

// A transport-neutral HTTP request/response pair. `HttpServer` parses raw
// HTTP/1.1 into a `Request`; `ApiClientLive` renders a `Request` into a curl
// call. Neither side ever hand-writes a path or body.
struct Request {
  std::string method = "GET";
  std::string path;
  std::string query; // raw, without the '?'
  std::string body;

  bool operator==(const Request &) const = default;
};

struct Response {
  int status = 200;
  std::string contentType = "application/json";
  std::string body;

  bool operator==(const Response &) const = default;
};

// Server side: recognize a request. Returns nullopt for unknown routes or
// bodies that don't decode (the caller answers 404 / 400).
std::optional<Route> match(const Request &request);

// Client side: render a route as the request the server will match.
Request print(const Route &route);

// Wire codecs (shared shapes for both directions).
std::string encodeLeaderboardEntries(const std::vector<SharedModels::LeaderboardEntry> &entries);
std::optional<std::vector<SharedModels::LeaderboardEntry>>
decodeLeaderboardEntries(std::string_view json);
std::string encodeScoreSubmission(const SharedModels::ScoreSubmission &submission);
std::optional<SharedModels::ScoreSubmission> decodeScoreSubmission(std::string_view json);

} // namespace ServerRouter
