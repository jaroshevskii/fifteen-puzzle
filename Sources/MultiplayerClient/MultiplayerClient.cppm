export module MultiplayerClient;

import std;
import Dependencies;
import MultiplayerCore;

// Interface for the realtime multiplayer connection — the client-side
// dependency the MultiplayerFeature talks to. The live (TCP) implementation
// lives in `MultiplayerClientLive`; tests inject a stub that scripts server
// events. `connect` blocks streaming events into the callback, which is
// exactly the shape of a `store.addTask` background task; the feature cancels
// it by id to disconnect.
export namespace MultiplayerClient {

// Connection-level wrapper around the protocol's ServerMessage stream.
struct Connected {
  bool operator==(const Connected &) const = default;
};
struct Failed { // could not reach the server at all
  bool operator==(const Failed &) const = default;
};
struct Closed { // the server went away mid-session
  bool operator==(const Closed &) const = default;
};
struct Received {
  MultiplayerCore::ServerMessage message;
  bool operator==(const Received &) const = default;
};

using Event = std::variant<Connected, Failed, Closed, Received>;

struct Client {
  // Connects, sends `Join{name, gridSize}`, and streams events into `onEvent`
  // until the server closes the connection or `stop` is requested (which sends
  // a polite `Leave` first). Blocking — run it inside `store.addTask`.
  std::function<void(std::string name, int gridSize, std::function<void(Event)> onEvent,
                     std::stop_token stop)>
      connect = [](std::string, int, std::function<void(Event)> onEvent, std::stop_token) {
        onEvent(Failed{});
      };

  // Sends a move on the current connection. A no-op when not connected.
  std::function<void(int index)> sendMove = [](int) {};
};

struct Key : Dependencies::DependencyKey<Key, Client> {
  // The real implementation is supplied by MultiplayerClientLive at launch.
  static Client liveValue() { return Client{}; }
  // Tests default to an immediate connection failure, so a test that forgets
  // to stub the client exercises the failure path instead of the network.
  static Client testValue() { return Client{}; }
};

} // namespace MultiplayerClient
