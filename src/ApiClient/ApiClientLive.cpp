module;

// HTTP + JSON live in this implementation unit's global module fragment, so
// they stay private to this TU and never reach importers (which would clash
// with `import std`). curl is a C header; the JSON header is C++ — both are
// fine here because an implementation unit's GMF is not reachable.
#include <curl/curl.h>
#include <nlohmann/json.hpp>

module ApiClientLive; // implementation unit

import std;
import ApiClient;
import SharedModels;

namespace ApiClient {

namespace {

using nlohmann::json;

// No server is bundled; point FIFTEEN_API_BASE_URL at a real deployment. With
// the default (or an unreachable host) calls fail as ApiError::offline and the
// app shows local-only data.
constexpr std::string_view kDefaultBaseUrl = "http://localhost:8080";

// Process-wide curl init/cleanup. curl_global_init is not thread-safe, so the
// live client is built once on the main thread (from prepareDependencies)
// before any background task runs. Held by shared_ptr so cleanup happens only
// when the last client copy (and any in-flight task capture) is gone.
class CurlGlobal {
public:
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
  CurlGlobal(const CurlGlobal &) = delete;
  CurlGlobal &operator=(const CurlGlobal &) = delete;
};

std::string resolveBaseUrl(const std::string &explicitUrl) {
  if (!explicitUrl.empty()) {
    return explicitUrl;
  }
  if (const char *env = std::getenv("FIFTEEN_API_BASE_URL"); env && *env) {
    return env;
  }
  return std::string(kDefaultBaseUrl);
}

std::size_t writeBody(char *ptr, std::size_t size, std::size_t nmemb, void *userdata) {
  auto *out = static_cast<std::string *>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

// Cooperative cancellation: returning non-zero from the progress callback
// aborts the transfer, which we map to ApiError::cancelled.
int onProgress(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  auto *stop = static_cast<std::stop_token *>(clientp);
  return stop->stop_requested() ? 1 : 0;
}

struct Response {
  long status = 0;
  std::string body;
  bool transportOk = false;
};

// One self-contained transfer with its own easy handle (libcurl easy handles
// are single-thread-only, so per-call handles are the safe pattern). A non-null
// `postBody` makes it a JSON POST.
Response perform(const std::string &url, const char *postBody, std::stop_token &stop) {
  Response response;
  CURL *curl = curl_easy_init();
  if (!curl) {
    return response;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeBody);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); // thread-safe DNS timeouts
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &onProgress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stop);

  curl_slist *headers = nullptr;
  if (postBody != nullptr) {
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody);
  }

  if (curl_easy_perform(curl) == CURLE_OK) {
    response.transportOk = true;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
  }

  if (headers != nullptr) {
    curl_slist_free_all(headers);
  }
  curl_easy_cleanup(curl);
  return response;
}

bool isSuccess(long status) { return status >= 200 && status < 300; }

} // namespace

Client live(std::string explicitUrl) {
  auto global = std::make_shared<CurlGlobal>();
  const std::string baseUrl = resolveBaseUrl(explicitUrl);

  return Client{
      .fetchLeaderboard = [global, baseUrl](int gridSize, std::stop_token stop)
          -> std::expected<std::vector<SharedModels::LeaderboardEntry>, ApiError> {
        if (stop.stop_requested()) {
          return std::unexpected(ApiError::cancelled);
        }
        const std::string url = std::format("{}/leaderboard?size={}", baseUrl, gridSize);
        const Response response = perform(url, nullptr, stop);
        if (stop.stop_requested()) {
          return std::unexpected(ApiError::cancelled);
        }
        if (!response.transportOk) {
          return std::unexpected(ApiError::offline);
        }
        if (!isSuccess(response.status)) {
          return std::unexpected(ApiError::httpError);
        }
        try {
          const json doc = json::parse(response.body);
          std::vector<SharedModels::LeaderboardEntry> entries;
          entries.reserve(doc.size());
          for (const auto &item : doc) {
            entries.push_back(
                SharedModels::LeaderboardEntry{.name = item.at("name").get<std::string>(),
                                               .gridSize = gridSize,
                                               .moves = item.at("moves").get<int>(),
                                               .duration = item.at("duration").get<int>(),
                                               .playedAt = item.value("playedAt", 0.0)});
          }
          return entries;
        } catch (const json::exception &) {
          return std::unexpected(ApiError::decodingError);
        }
      },
      .submitScore = [global, baseUrl](SharedModels::ScoreSubmission submission,
                                       std::stop_token stop) -> std::expected<void, ApiError> {
        if (stop.stop_requested()) {
          return std::unexpected(ApiError::cancelled);
        }
        const json body = {{"name", submission.name},
                           {"gridSize", submission.gridSize},
                           {"moves", submission.moves},
                           {"duration", submission.duration},
                           {"playedAt", submission.playedAt}};
        const std::string payload = body.dump();
        const std::string url = std::format("{}/scores", baseUrl);
        const Response response = perform(url, payload.c_str(), stop);
        if (stop.stop_requested()) {
          return std::unexpected(ApiError::cancelled);
        }
        if (!response.transportOk) {
          return std::unexpected(ApiError::offline);
        }
        if (!isSuccess(response.status)) {
          return std::unexpected(ApiError::httpError);
        }
        return {};
      }};
}

} // namespace ApiClient
