export module LiveFeature;

import std;
import ComposableArchitecture;
import MultiplayerClient;
import MultiplayerCore;

// The live-games feed — the "watch what's happening online" screen. It
// subscribes to the server's observer channel (a `store.addTask` running the
// MultiplayerClient's `observe`) and folds the pushed feed messages
// (Presence / MatchStarted / MatchEnded) into a plain state machine: the
// current counts, the list of in-progress matches, and a rolling ticker of
// recently finished games. A contained, exhaustively-testable spectator-lite —
// the seed of full board spectating.
export namespace LiveFeature {

enum class Phase : std::uint8_t { connecting, live, failed };

struct Match {
  int matchId = 0;
  int gridSize = 4;
  std::string playerA;
  std::string playerB;
  bool operator==(const Match &) const = default;
};

struct FinishedMatch {
  std::string winnerName;
  int gridSize = 4;
  int durationSeconds = 0;
  bool operator==(const FinishedMatch &) const = default;
};

struct State {
  Phase phase = Phase::connecting;
  int online = 0;
  int racing = 0;
  int waiting = 0;
  std::vector<Match> matches;        // in progress, newest first
  std::vector<FinishedMatch> recent; // finished, newest first, capped
  bool operator==(const State &) const = default;
};

// The most recent finished games kept in the ticker.
constexpr std::size_t maxRecent = 8;

struct Appeared {};
struct ClientEvent {
  MultiplayerClient::Event event;
};

using Action = std::variant<Appeared, ClientEvent>;

// Cancelling this id disconnects the observer channel (the parent does it on
// dismiss).
constexpr std::string_view kConnectionCancelId = "live-connection";

State initialState();
ComposableArchitecture::Feature<State, Action> body();

} // namespace LiveFeature
