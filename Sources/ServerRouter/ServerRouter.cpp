module;

// JSON lives in this implementation unit's global module fragment, so it stays
// private to this TU and never reaches importers (which would clash with
// `import std` — see ApiClientLive for the same pattern).
#include <nlohmann/json.hpp>

module ServerRouter; // implementation unit

import std;
import SharedModels;

namespace ServerRouter {

namespace {

using nlohmann::json;

// Value of `name` in a query string like "size=4&x=y", or nullopt.
std::optional<std::string_view> queryValue(std::string_view query, std::string_view name) {
  while (!query.empty()) {
    const std::size_t amp = query.find('&');
    const std::string_view pair = query.substr(0, amp);
    const std::size_t eq = pair.find('=');
    if (eq != std::string_view::npos && pair.substr(0, eq) == name) {
      return pair.substr(eq + 1);
    }
    query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
  }
  return std::nullopt;
}

std::optional<int> parseInt(std::string_view text) {
  int value = 0;
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

} // namespace

std::optional<Route> match(const Request &request) {
  if (request.method == "GET" && request.path == "/leaderboard") {
    FetchLeaderboard route;
    if (const auto size = queryValue(request.query, "size")) {
      const auto gridSize = parseInt(*size);
      if (!gridSize.has_value()) {
        return std::nullopt;
      }
      route.gridSize = *gridSize;
    }
    return Route{route};
  }
  if (request.method == "POST" && request.path == "/scores") {
    auto submission = decodeScoreSubmission(request.body);
    if (!submission.has_value()) {
      return std::nullopt;
    }
    return Route{SubmitScore{std::move(*submission)}};
  }
  return std::nullopt;
}

Request print(const Route &route) {
  return std::visit(
      [](auto &&value) -> Request {
        using V = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<V, FetchLeaderboard>) {
          return Request{.method = "GET",
                         .path = "/leaderboard",
                         .query = std::format("size={}", value.gridSize)};
        } else if constexpr (std::is_same_v<V, SubmitScore>) {
          return Request{
              .method = "POST", .path = "/scores", .body = encodeScoreSubmission(value.submission)};
        }
      },
      route);
}

std::string encodeLeaderboardEntries(const std::vector<SharedModels::LeaderboardEntry> &entries) {
  json doc = json::array();
  for (const auto &entry : entries) {
    doc.push_back({{"name", entry.name},
                   {"gridSize", entry.gridSize},
                   {"moves", entry.moves},
                   {"duration", entry.duration},
                   {"playedAt", entry.playedAt}});
  }
  return doc.dump();
}

std::optional<std::vector<SharedModels::LeaderboardEntry>>
decodeLeaderboardEntries(std::string_view text) {
  try {
    const json doc = json::parse(text);
    std::vector<SharedModels::LeaderboardEntry> entries;
    entries.reserve(doc.size());
    for (const auto &item : doc) {
      entries.push_back(SharedModels::LeaderboardEntry{.name = item.at("name").get<std::string>(),
                                                       .gridSize = item.value("gridSize", 0),
                                                       .moves = item.at("moves").get<int>(),
                                                       .duration = item.at("duration").get<int>(),
                                                       .playedAt = item.value("playedAt", 0.0)});
    }
    return entries;
  } catch (const json::exception &) {
    return std::nullopt;
  }
}

std::string encodeScoreSubmission(const SharedModels::ScoreSubmission &submission) {
  const json doc = {{"name", submission.name},
                    {"gridSize", submission.gridSize},
                    {"moves", submission.moves},
                    {"duration", submission.duration},
                    {"playedAt", submission.playedAt}};
  return doc.dump();
}

std::optional<SharedModels::ScoreSubmission> decodeScoreSubmission(std::string_view text) {
  try {
    const json doc = json::parse(text);
    return SharedModels::ScoreSubmission{.name = doc.at("name").get<std::string>(),
                                         .gridSize = doc.at("gridSize").get<int>(),
                                         .moves = doc.at("moves").get<int>(),
                                         .duration = doc.at("duration").get<int>(),
                                         .playedAt = doc.value("playedAt", 0.0)};
  } catch (const json::exception &) {
    return std::nullopt;
  }
}

} // namespace ServerRouter
