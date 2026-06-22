export module ApiClientLive;

import std;
import ApiClient;

// Interface unit: declares the live factory only. The HTTP and JSON libraries
// are confined to the implementation unit (ApiClientLive.cpp), so neither curl
// nor the JSON headers enter this module's reachable interface — keeping
// `import std` consumers free of the C++-header-in-GMF clashes.
export namespace ApiClient {

// Returns a libcurl-backed client talking to `baseUrl`. If `baseUrl` is empty,
// it is taken from the FIFTEEN_API_BASE_URL environment variable, else a local
// default. The returned client is safe to call from background tasks; if the
// server is unreachable, calls return ApiError::offline (never throw), so the
// app degrades to local-only data.
Client live(std::string baseUrl = {});

} // namespace ApiClient
