module;

// curl lives in this implementation unit's global module fragment, so it stays
// private to this TU and never reaches importers (which would clash with
// `import std`). It is a C header, so the GMF is the right place for it.
#include <curl/curl.h>

module ApiClientLive; // implementation unit

import std;
import ApiClient;
import ServerRouter;
import SharedModels;

namespace ApiClient {

namespace {

// No server is bundled by default; run the FifteenServer executable from this
// repo (see Sources/server) or point FIFTEEN_API_BASE_URL at a deployment.
// With an unreachable host calls fail as ApiError::offline and the app shows
// local-only data.
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

// Performs one route's request, rendered by the shared ServerRouter — the
// client never hand-writes a path or body. One self-contained transfer with
// its own easy handle (libcurl easy handles are single-thread-only, so
// per-call handles are the safe pattern).
Response perform(const std::string &baseUrl, const ServerRouter::Route &route,
                 std::stop_token &stop) {
  const ServerRouter::Request request = ServerRouter::print(route);
  std::string url = baseUrl + request.path;
  if (!request.query.empty()) {
    url += "?" + request.query;
  }

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
  if (request.method == "POST") {
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, request.body.c_str());
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
        const Response response =
            perform(baseUrl, ServerRouter::FetchLeaderboard{.gridSize = gridSize}, stop);
        if (stop.stop_requested()) {
          return std::unexpected(ApiError::cancelled);
        }
        if (!response.transportOk) {
          return std::unexpected(ApiError::offline);
        }
        if (!isSuccess(response.status)) {
          return std::unexpected(ApiError::httpError);
        }
        auto entries = ServerRouter::decodeLeaderboardEntries(response.body);
        if (!entries.has_value()) {
          return std::unexpected(ApiError::decodingError);
        }
        for (auto &entry : *entries) {
          if (entry.gridSize == 0) {
            entry.gridSize = gridSize; // implied by the request when a server omits it
          }
        }
        return std::move(*entries);
      },
      .submitScore = [global, baseUrl](SharedModels::ScoreSubmission submission,
                                       std::stop_token stop) -> std::expected<void, ApiError> {
        if (stop.stop_requested()) {
          return std::unexpected(ApiError::cancelled);
        }
        const Response response =
            perform(baseUrl, ServerRouter::SubmitScore{.submission = std::move(submission)}, stop);
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
