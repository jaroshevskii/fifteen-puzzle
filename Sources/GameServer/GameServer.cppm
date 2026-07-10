export module GameServer;

import std;
import Dependencies;
import MultiplayerCore;
import PuzzleCore;
import SharedModels;

// The realtime multiplayer referee. The `Engine` is pure, single-threaded
// logic — feed it player messages, get back the messages to deliver and any
// server-verified results — so the tests drive it directly, and the socket
// shell (`run`, in the implementation unit) stays a dumb transport loop.
//
// Anti-cheat is structural, isowords-style: the server deals every board (the
// scramble seed) and re-plays every reported move on its own copy with the
// shared PuzzleCore rules. A client cannot claim a win — the referee notices
// the solved board itself. Time and randomness flow through the Dependencies
// library, so tests pin the clock and the seeds.
export namespace GameServer {

using PlayerId = int;

struct Outbound {
  PlayerId player = 0;
  MultiplayerCore::ServerMessage message;
};

struct Output {
  std::vector<Outbound> messages;
  // Server-verified finished games: the winner's row, ready for the
  // leaderboard database. (Verified because the engine itself replayed every
  // move that produced it.)
  std::vector<SharedModels::ScoreSubmission> results;
};

class Engine {
public:
  Output join(PlayerId player, std::string name, int gridSize);
  Output move(PlayerId player, int index);
  // Covers both an explicit Leave message and a dropped connection.
  Output leave(PlayerId player);
  // Subscribes `player` to the live feed: returns a Presence snapshot plus a
  // MatchStarted for every match already in progress.
  Output observe(PlayerId player);

private:
  struct Board {
    std::string name;
    std::vector<std::string> tiles;
    std::vector<int> history;
  };

  struct Room {
    int matchId = 0;
    int grid = 4;
    std::uint64_t seed = 0;
    double startedAt = 0.0;
    bool finished = false;
    std::map<PlayerId, Board> boards; // exactly two entries
  };

  struct WaitingPlayer {
    PlayerId player = 0;
    std::string name;
  };

  Room *roomOf(PlayerId player);
  Output finishRoom(Room &room, PlayerId winner);

  // Live-feed helpers.
  MultiplayerCore::Presence presence() const;
  void broadcastToObservers(Output &output, MultiplayerCore::ServerMessage message) const;

  std::map<int, WaitingPlayer> waitingByGrid_;      // one queued player per board size
  std::map<PlayerId, std::shared_ptr<Room>> rooms_; // both players point at the same room
  std::set<PlayerId> observers_;                    // subscribed to the live feed
  int nextMatchId_ = 1;
};

// The socket shell: accepts connections on `port`, decodes line-JSON client
// messages, drives a mutex-guarded Engine, and delivers its outbound messages.
// `onResult` receives each server-verified result (the server main persists
// them to the leaderboard database). `maxConnections` caps concurrent workers
// (<= 0 means unbounded); connections over the cap get a typed `ServerFull`
// and are closed. Finished worker threads are reaped as new ones arrive.
// Returns false if the port cannot be bound; otherwise blocks until `stop`.
bool run(int port, std::function<void(const SharedModels::ScoreSubmission &)> onResult,
         int maxConnections, std::stop_token stop);

} // namespace GameServer
