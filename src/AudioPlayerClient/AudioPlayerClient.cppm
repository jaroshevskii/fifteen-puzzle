export module AudioPlayerClient;

import std;
import Dependencies;

// Interface for the audio player dependency, mirroring isowords'
// `AudioPlayerClient`. This module is free of any audio-backend headers: the
// live, OpenAL-backed implementation lives in `AudioPlayerClientLive` and is
// wired in at app launch via `prepareDependencies`, while tests and previews
// use the inert default.
export namespace AudioPlayerClient {

enum class Sound : std::uint8_t {
  tick,
};

struct Client {
  std::function<void(Sound)> play = [](Sound) {};
};

struct Key : Dependencies::DependencyKey<Key, Client> {
  // No-op by default; the live sound engine is supplied by
  // `AudioPlayerClientLive`.
  static Client liveValue() { return Client{}; }
};

} // namespace AudioPlayerClient
