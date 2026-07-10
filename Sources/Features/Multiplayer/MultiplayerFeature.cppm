export module MultiplayerFeature;

import std;
import ComposableArchitecture;
import MultiplayerClient;
import MultiplayerCore;
import PuzzleCore;

// The realtime race feature — the analog of isowords' MultiplayerFeature,
// adapted to this game's head-to-head mode. Presenting the screen dispatches
// `Appeared` (the house lifecycle pattern for `ifCaseLet` children), which
// starts the blocking connection loop as a cancellable background task. Every
// server event comes back through one `ClientEvent` action, so the whole
// session is a plain, exhaustively-testable state machine.
//
// The server is the referee: the board is dealt from its seed, every local
// slide is reported with `sendMove`, and the race ends only when the server
// says so (`Finished`) — a locally solved board just waits for confirmation.
export namespace MultiplayerFeature {

enum class Phase : std::uint8_t {
  connecting, // dialing the server
  queued,     // in the matchmaking queue
  racing,     // both boards dealt, clock running
  finished,   // server declared a winner (or the opponent left)
  failed,     // could not connect, or the connection dropped mid-race
};

struct State {
  std::string playerName = "Player";
  int gridSize = PuzzleCore::minGrid;
  Phase phase = Phase::connecting;

  std::string opponentName;
  std::vector<std::string> tiles; // this player's board (dealt from the seed)
  std::vector<int> moveHistory;
  int opponentMoveCount = 0;
  // The opponent's board, replayed locally: dealt from the same seed, then
  // every (server-validated) relayed move applied with the shared PuzzleCore
  // rules — so the mini preview always mirrors the referee's copy.
  std::vector<std::string> opponentTiles;

  int secondsElapsed = 0;
  std::optional<double> startDate;

  // Set while `phase == finished`.
  bool youWon = false;
  bool opponentLeft = false;
  std::string winnerName;
  int finalDurationSeconds = 0;

  // Set while `phase == failed` when the server refused us at capacity, so the
  // view can show "server full — try again" instead of a generic error.
  bool serverWasFull = false;

  bool operator==(const State &) const = default;
};

struct Appeared {};
struct ClientEvent {
  MultiplayerClient::Event event;
};
struct RematchTapped {};
struct TileTapped {
  int index = 0;
};
struct TimerTicked {};

using Action = std::variant<Appeared, ClientEvent, RematchTapped, TileTapped, TimerTicked>;

// Cancelling this id tears down the connection (the parent does it on
// dismiss; a rematch does it before reconnecting).
constexpr std::string_view kConnectionCancelId = "multiplayer-connection";

State initialState(std::string playerName, int gridSize);
ComposableArchitecture::Feature<State, Action> body();

// Whether this player's board is solved locally (the "waiting for the
// referee" moment before the server's `Finished` arrives).
bool isBoardSolved(const State &state);

} // namespace MultiplayerFeature
