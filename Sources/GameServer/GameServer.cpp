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

MultiplayerCore::Presence Engine::presence() const {
  std::set<const Room *> active;
  for (const auto &[id, room] : rooms_) {
    if (!room->finished) {
      active.insert(room.get());
    }
  }
  const int racing = static_cast<int>(active.size()) * 2;
  const int waiting = static_cast<int>(waitingByGrid_.size());
  const int observing = static_cast<int>(observers_.size());
  return MultiplayerCore::Presence{
      .online = racing + waiting + observing, .racing = racing, .waiting = waiting};
}

void Engine::broadcastToObservers(Output &output, MultiplayerCore::ServerMessage message) const {
  for (const PlayerId observer : observers_) {
    output.messages.push_back({observer, message});
  }
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
    Output output{.messages = {{player, MultiplayerCore::Queued{}}}};
    broadcastToObservers(output, presence()); // the queue grew
    return output;
  }

  // Match found: deal both players the same board via a shared scramble seed.
  const WaitingPlayer opponent = waiting->second;
  waitingByGrid_.erase(waiting);

  Dependencies::Dependency<Dependencies::RandomNumberGeneratorKey> rng;
  Dependencies::Dependency<Dependencies::DateGeneratorKey> date;

  auto room = std::make_shared<Room>();
  room->matchId = nextMatchId_++;
  room->grid = grid;
  room->seed = (*rng)();
  room->startedAt = date->now();
  room->boards[opponent.player] =
      Board{.name = opponent.name, .tiles = PuzzleCore::scrambled(grid, room->seed)};
  room->boards[player] = Board{.name = name, .tiles = PuzzleCore::scrambled(grid, room->seed)};
  rooms_[opponent.player] = room;
  rooms_[player] = room;

  Output output{
      .messages = {
          {opponent.player,
           MultiplayerCore::Start{.seed = room->seed, .gridSize = grid, .opponentName = name}},
          {player, MultiplayerCore::Start{.seed = room->seed,
                                          .gridSize = grid,
                                          .opponentName = opponent.name}},
      }};
  // Announce the new match to the live feed (a queued player became a racer).
  broadcastToObservers(output, MultiplayerCore::MatchStarted{.matchId = room->matchId,
                                                             .gridSize = grid,
                                                             .playerA = opponent.name,
                                                             .playerB = name});
  broadcastToObservers(output, presence());
  return output;
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
  // Announce the finish to the live feed.
  broadcastToObservers(output, MultiplayerCore::MatchEnded{.matchId = room.matchId,
                                                           .winnerName = winnerBoard.name,
                                                           .gridSize = room.grid,
                                                           .durationSeconds = duration});
  broadcastToObservers(output, presence());
  return output;
}

Output Engine::observe(PlayerId player) {
  observers_.insert(player);

  Output output;
  // The subscriber gets a snapshot of every match already in progress...
  std::set<const Room *> seen;
  for (const auto &[id, room] : rooms_) {
    if (room->finished || seen.contains(room.get())) {
      continue;
    }
    seen.insert(room.get());
    const auto &boards = room->boards;
    const auto first = boards.begin();
    const auto second = std::next(first);
    output.messages.push_back(
        {player, MultiplayerCore::MatchStarted{
                     .matchId = room->matchId,
                     .gridSize = room->grid,
                     .playerA = first->second.name,
                     .playerB = second != boards.end() ? second->second.name : std::string{}}});
  }
  // ...and everyone (including the new subscriber) gets the fresh count.
  broadcastToObservers(output, presence());
  return output;
}

Output Engine::leave(PlayerId player) {
  const bool wasObserver = observers_.erase(player) > 0;

  // Queued and never matched: just drop out of the queue.
  for (auto it = waitingByGrid_.begin(); it != waitingByGrid_.end(); ++it) {
    if (it->second.player == player) {
      waitingByGrid_.erase(it);
      Output output;
      broadcastToObservers(output, presence());
      return output;
    }
  }

  const auto it = rooms_.find(player);
  if (it == rooms_.end()) {
    // Not racing or queued; if an observer left, the count changed.
    Output output;
    if (wasObserver) {
      broadcastToObservers(output, presence());
    }
    return output;
  }
  const std::shared_ptr<Room> room = it->second;
  rooms_.erase(it);

  Output output;
  if (!room->finished) {
    room->finished = true; // a walkover ends the race; no result is recorded
    std::string remainingName;
    for (const auto &[id, board] : room->boards) {
      if (id != player) {
        remainingName = board.name;
        if (rooms_.contains(id)) {
          output.messages.push_back({id, MultiplayerCore::OpponentLeft{}});
        }
      }
    }
    // The feed sees the walkover as a match ending (the remaining player wins).
    broadcastToObservers(output, MultiplayerCore::MatchEnded{.matchId = room->matchId,
                                                             .winnerName = remainingName,
                                                             .gridSize = room->grid,
                                                             .durationSeconds = 0});
  }
  broadcastToObservers(output, presence());
  return output;
}

// --- Socket shell --------------------------------------------------------------

namespace {

struct Shared {
  std::mutex mutex; // guards the engine and the connection table
  Engine engine;
  std::map<PlayerId, std::shared_ptr<TcpSocket::Connection>> connections;
  std::function<void(const SharedModels::ScoreSubmission &)> onResult;
  // Live worker count, for the connection cap. Incremented on the accept
  // thread before a worker is spawned, decremented by the worker on exit.
  std::atomic<int> activeConnections{0};

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
            } else if constexpr (std::is_same_v<V, MultiplayerCore::Observe>) {
              shared->deliver(shared->engine.observe(player));
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

  {
    std::scoped_lock lock(shared->mutex);
    shared->deliver(shared->engine.leave(player)); // no-op if the player already left
    shared->connections.erase(player);
  }
  connection->close();
  shared->activeConnections.fetch_sub(1, std::memory_order_release);
}

} // namespace

bool run(int port, std::function<void(const SharedModels::ScoreSubmission &)> onResult,
         int maxConnections, std::stop_token stop) {
  auto listener = TcpSocket::Listener::bind(port);
  if (!listener.has_value()) {
    return false;
  }
  std::stop_callback unblock(stop, [&listener] { listener->close(); });

  auto shared = std::make_shared<Shared>();
  shared->onResult = std::move(onResult);

  // A worker plus a flag it raises when it returns, so the accept loop can
  // reap finished threads instead of letting the vector grow forever.
  struct Worker {
    std::jthread thread;
    std::shared_ptr<std::atomic<bool>> finished;
  };
  std::vector<Worker> workers;
  PlayerId nextPlayer = 1;

  while (!stop.stop_requested()) {
    // Reap workers that have finished since the last accept. A raised flag
    // means the thread body returned, so joining it (via ~jthread on erase)
    // does not block.
    std::erase_if(workers, [](const Worker &worker) {
      return worker.finished->load(std::memory_order_acquire);
    });

    auto accepted = listener->accept();
    if (!accepted.has_value()) {
      continue; // listener closed (shutdown) or transient failure
    }
    auto connection = std::make_shared<TcpSocket::Connection>(std::move(*accepted));

    // Enforce the connection cap: refuse politely (typed ServerFull) rather
    // than spawn unbounded threads.
    if (maxConnections > 0 &&
        shared->activeConnections.load(std::memory_order_acquire) >= maxConnections) {
      connection->sendAll(
          MultiplayerCore::encode(MultiplayerCore::ServerMessage{MultiplayerCore::ServerFull{}}) +
          "\n");
      connection->close();
      continue;
    }

    const PlayerId player = nextPlayer++;
    shared->activeConnections.fetch_add(1, std::memory_order_release);
    {
      std::scoped_lock lock(shared->mutex);
      shared->connections[player] = connection;
    }
    auto finished = std::make_shared<std::atomic<bool>>(false);
    workers.push_back(Worker{
        .thread = std::jthread([shared, player, connection, finished, stop](std::stop_token) {
          servePlayer(shared, player, connection, stop);
          finished->store(true, std::memory_order_release);
        }),
        .finished = finished});
  }
  // jthread destructors join; each worker notices `stop` within its receive
  // timeout and unwinds.
  return true;
}

} // namespace GameServer
