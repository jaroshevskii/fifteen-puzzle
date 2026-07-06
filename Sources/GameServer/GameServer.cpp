module GameServer; // implementation unit

import std;
import Dependencies;
import MultiplayerCore;
import PuzzleCore;
import SharedModels;
import TcpSocket;

namespace GameServer {

// --- Engine ------------------------------------------------------------------

Engine::Room *Engine::roomOf(PlayerId player) {
  const auto it = rooms_.find(player);
  return it == rooms_.end() ? nullptr : it->second.get();
}

Output Engine::join(PlayerId player, std::string name, int gridSize) {
  if (roomOf(player) != nullptr) {
    return {}; // already in a game; ignore a duplicate join
  }
  const int grid = std::clamp(gridSize, PuzzleCore::minGrid, PuzzleCore::maxGrid);
  if (name.empty()) {
    name = "Player";
  }

  const auto waiting = waitingByGrid_.find(grid);
  if (waiting == waitingByGrid_.end() || waiting->second.player == player) {
    // Nobody (else) to race yet — queue this player for the board size.
    waitingByGrid_[grid] = WaitingPlayer{.player = player, .name = std::move(name)};
    return Output{.messages = {{player, MultiplayerCore::Queued{}}}};
  }

  // Match found: deal both players the same board via a shared scramble seed.
  const WaitingPlayer opponent = waiting->second;
  waitingByGrid_.erase(waiting);

  Dependencies::Dependency<Dependencies::RandomNumberGeneratorKey> rng;
  Dependencies::Dependency<Dependencies::DateGeneratorKey> date;

  auto room = std::make_shared<Room>();
  room->grid = grid;
  room->seed = (*rng)();
  room->startedAt = date->now();
  room->boards[opponent.player] =
      Board{.name = opponent.name, .tiles = PuzzleCore::scrambled(grid, room->seed)};
  room->boards[player] = Board{.name = name, .tiles = PuzzleCore::scrambled(grid, room->seed)};
  rooms_[opponent.player] = room;
  rooms_[player] = room;

  return Output{
      .messages = {
          {opponent.player,
           MultiplayerCore::Start{.seed = room->seed, .gridSize = grid, .opponentName = name}},
          {player, MultiplayerCore::Start{.seed = room->seed,
                                          .gridSize = grid,
                                          .opponentName = opponent.name}},
      }};
}

Output Engine::move(PlayerId player, int index) {
  Room *room = roomOf(player);
  if (room == nullptr || room->finished) {
    return {};
  }

  Board &board = room->boards[player];
  // The referee replays the move on its own copy of the board; an illegal
  // move is rejected instead of trusted.
  if (!PuzzleCore::slide(board.tiles, board.history, room->grid, index)) {
    return Output{.messages = {{player, MultiplayerCore::MoveRejected{.index = index}}}};
  }

  Output output;
  for (const auto &[id, other] : room->boards) {
    if (id != player) {
      output.messages.push_back(
          {id, MultiplayerCore::OpponentMoved{
                   .index = index, .moveCount = static_cast<int>(board.history.size())}});
    }
  }

  if (PuzzleCore::isSolved(board.tiles, room->grid)) {
    Output finish = finishRoom(*room, player);
    output.messages.insert(output.messages.end(), finish.messages.begin(), finish.messages.end());
    output.results = std::move(finish.results);
  }
  return output;
}

Output Engine::finishRoom(Room &room, PlayerId winner) {
  Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
  room.finished = true;

  const Board &winnerBoard = room.boards[winner];
  const double now = date->now();
  const int duration = static_cast<int>(now - room.startedAt);
  const int moves = static_cast<int>(winnerBoard.history.size());

  Output output;
  for (const auto &[id, board] : room.boards) {
    output.messages.push_back({id, MultiplayerCore::Finished{.youWon = id == winner,
                                                             .winnerName = winnerBoard.name,
                                                             .durationSeconds = duration,
                                                             .moves = moves}});
  }
  output.results.push_back(SharedModels::ScoreSubmission{.name = winnerBoard.name,
                                                         .gridSize = room.grid,
                                                         .moves = moves,
                                                         .duration = duration,
                                                         .playedAt = now});
  return output;
}

Output Engine::leave(PlayerId player) {
  // Queued and never matched: just drop out of the queue.
  for (auto it = waitingByGrid_.begin(); it != waitingByGrid_.end(); ++it) {
    if (it->second.player == player) {
      waitingByGrid_.erase(it);
      return {};
    }
  }

  const auto it = rooms_.find(player);
  if (it == rooms_.end()) {
    return {};
  }
  const std::shared_ptr<Room> room = it->second;
  rooms_.erase(it);

  Output output;
  if (!room->finished) {
    room->finished = true; // a walkover ends the race; no result is recorded
    for (const auto &[id, board] : room->boards) {
      if (id != player && rooms_.contains(id)) {
        output.messages.push_back({id, MultiplayerCore::OpponentLeft{}});
      }
    }
  }
  return output;
}

// --- Socket shell --------------------------------------------------------------

namespace {

struct Shared {
  std::mutex mutex; // guards the engine and the connection table
  Engine engine;
  std::map<PlayerId, std::shared_ptr<TcpSocket::Connection>> connections;
  std::function<void(const SharedModels::ScoreSubmission &)> onResult;

  // Must be called with `mutex` held.
  void deliver(const Output &output) {
    for (const auto &outbound : output.messages) {
      if (const auto it = connections.find(outbound.player); it != connections.end()) {
        it->second->sendAll(MultiplayerCore::encode(outbound.message) + "\n");
      }
    }
    for (const auto &result : output.results) {
      if (onResult) {
        onResult(result);
      }
    }
  }
};

void servePlayer(std::shared_ptr<Shared> shared, PlayerId player,
                 std::shared_ptr<TcpSocket::Connection> connection, std::stop_token stop) {
  // A short receive timeout keeps the blocking read responsive to shutdown.
  connection->setReceiveTimeout(std::chrono::milliseconds(250));

  while (!stop.stop_requested()) {
    const auto read = connection->readLine();
    if (read.status == TcpSocket::ReadStatus::timedOut) {
      continue;
    }
    if (read.status == TcpSocket::ReadStatus::closed) {
      break;
    }
    const auto message = MultiplayerCore::decodeClientMessage(read.line);
    if (!message.has_value()) {
      continue; // garbage line; ignore rather than kill the connection
    }
    bool left = false;
    {
      std::scoped_lock lock(shared->mutex);
      std::visit(
          [&](auto &&value) {
            using V = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<V, MultiplayerCore::Join>) {
              shared->deliver(shared->engine.join(player, value.name, value.gridSize));
            } else if constexpr (std::is_same_v<V, MultiplayerCore::Move>) {
              shared->deliver(shared->engine.move(player, value.index));
            } else if constexpr (std::is_same_v<V, MultiplayerCore::Leave>) {
              shared->deliver(shared->engine.leave(player));
              left = true;
            }
          },
          *message);
    }
    if (left) {
      break;
    }
  }

  std::scoped_lock lock(shared->mutex);
  shared->deliver(shared->engine.leave(player)); // no-op if the player already left
  shared->connections.erase(player);
  connection->close();
}

} // namespace

bool run(int port, std::function<void(const SharedModels::ScoreSubmission &)> onResult,
         std::stop_token stop) {
  auto listener = TcpSocket::Listener::bind(port);
  if (!listener.has_value()) {
    return false;
  }
  std::stop_callback unblock(stop, [&listener] { listener->close(); });

  auto shared = std::make_shared<Shared>();
  shared->onResult = std::move(onResult);

  std::vector<std::jthread> workers;
  PlayerId nextPlayer = 1;

  while (!stop.stop_requested()) {
    auto accepted = listener->accept();
    if (!accepted.has_value()) {
      continue; // listener closed (shutdown) or transient failure
    }
    const PlayerId player = nextPlayer++;
    auto connection = std::make_shared<TcpSocket::Connection>(std::move(*accepted));
    {
      std::scoped_lock lock(shared->mutex);
      shared->connections[player] = connection;
    }
    workers.emplace_back([shared, player, connection, stop](std::stop_token) {
      servePlayer(shared, player, connection, stop);
    });
  }
  // jthread destructors join; each worker notices `stop` within its receive
  // timeout and unwinds.
  return true;
}

} // namespace GameServer
