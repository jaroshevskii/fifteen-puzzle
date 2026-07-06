export module MultiplayerCore;

import std;

// The multiplayer wire protocol, defined once and shared by the server
// (GameServer) and the client (MultiplayerClientLive) — the same
// single-definition idea as ServerRouter, but for the realtime race socket.
// Messages travel as one JSON object per line over TCP; the codecs live in
// the implementation unit so nlohmann/json stays out of the interface.
//
// The race protocol: a client `Join`s with a name and board size and is
// `Queued` until an opponent arrives; the server then deals both players the
// same board (a scramble seed) with `Start`. Each legal `Move` is re-played by
// the server on its own copy of that player's board (never trusted blindly)
// and relayed to the opponent as `OpponentMoved`. The server alone decides
// the win: when a board reaches the solved state it broadcasts `Finished`.
export namespace MultiplayerCore {

// --- client → server --------------------------------------------------------

struct Join {
  std::string name;
  int gridSize = 4;
  bool operator==(const Join &) const = default;
};

struct Move {
  int index = 0;
  bool operator==(const Move &) const = default;
};

struct Leave {
  bool operator==(const Leave &) const = default;
};

using ClientMessage = std::variant<Join, Move, Leave>;

// --- server → client ---------------------------------------------------------

struct Queued {
  bool operator==(const Queued &) const = default;
};

struct Start {
  std::uint64_t seed = 0; // both boards come from PuzzleCore::scrambled(grid, seed)
  int gridSize = 4;
  std::string opponentName;
  bool operator==(const Start &) const = default;
};

struct OpponentMoved {
  int index = 0;
  int moveCount = 0;
  bool operator==(const OpponentMoved &) const = default;
};

// The server refused a move that is illegal on its copy of the board. A
// well-behaved client never receives this; it exists so a buggy or dishonest
// client cannot desynchronize the referee.
struct MoveRejected {
  int index = 0;
  bool operator==(const MoveRejected &) const = default;
};

struct Finished {
  bool youWon = false;
  std::string winnerName;
  int durationSeconds = 0;
  int moves = 0;
  bool operator==(const Finished &) const = default;
};

struct OpponentLeft {
  bool operator==(const OpponentLeft &) const = default;
};

using ServerMessage =
    std::variant<Queued, Start, OpponentMoved, MoveRejected, Finished, OpponentLeft>;

// --- line codec --------------------------------------------------------------

std::string encode(const ClientMessage &message);
std::string encode(const ServerMessage &message);
std::optional<ClientMessage> decodeClientMessage(std::string_view line);
std::optional<ServerMessage> decodeServerMessage(std::string_view line);

} // namespace MultiplayerCore
