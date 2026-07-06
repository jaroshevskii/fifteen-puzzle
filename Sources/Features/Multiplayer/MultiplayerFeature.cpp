module MultiplayerFeature; // implementation unit

import std;
import ComposableArchitecture;
import Dependencies;
import MultiplayerClient;
import MultiplayerCore;
import PuzzleCore;

namespace MultiplayerFeature {

namespace {

// Resets the session state and starts the connection loop as a cancellable
// background task. Server events flow back as `ClientEvent` actions.
void startSession(State &state, ComposableArchitecture::Store<State, Action> &store) {
  state.phase = Phase::connecting;
  state.opponentName.clear();
  state.tiles.clear();
  state.moveHistory.clear();
  state.opponentMoveCount = 0;
  state.opponentTiles.clear();
  state.secondsElapsed = 0;
  state.startDate = std::nullopt;
  state.youWon = false;
  state.opponentLeft = false;
  state.winnerName.clear();
  state.finalDurationSeconds = 0;

  store.cancel(std::string(kConnectionCancelId)); // a rematch supersedes the old connection
  store.addTask(
      [name = state.playerName, gridSize = state.gridSize](
          ComposableArchitecture::Store<State, Action> &store, std::stop_token stop) {
        Dependencies::Dependency<MultiplayerClient::Key> client;
        client->connect(
            name, gridSize,
            [&store](MultiplayerClient::Event event) { store.send(ClientEvent{std::move(event)}); },
            std::move(stop));
      },
      std::string(kConnectionCancelId));
}

void handleServerMessage(State &state, const MultiplayerCore::ServerMessage &message) {
  Dependencies::Dependency<Dependencies::DateGeneratorKey> date;

  std::visit(
      [&](auto &&value) {
        using V = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<V, MultiplayerCore::Queued>) {
          state.phase = Phase::queued;
        } else if constexpr (std::is_same_v<V, MultiplayerCore::Start>) {
          // Deal this player's board from the room seed — the server and the
          // opponent build the identical board from the same number.
          state.phase = Phase::racing;
          state.gridSize = value.gridSize;
          state.opponentName = value.opponentName;
          state.tiles = PuzzleCore::scrambled(value.gridSize, value.seed);
          state.moveHistory.clear();
          state.opponentMoveCount = 0;
          state.opponentTiles = state.tiles; // same deal — replayed as moves arrive
          state.secondsElapsed = 0;
          state.startDate = date->now();
        } else if constexpr (std::is_same_v<V, MultiplayerCore::OpponentMoved>) {
          state.opponentMoveCount = value.moveCount;
          // Replay the relayed (already referee-validated) move on the local
          // copy, so the preview shows the opponent's live board.
          std::vector<int> ignoredHistory;
          PuzzleCore::slide(state.opponentTiles, ignoredHistory, state.gridSize, value.index);
        } else if constexpr (std::is_same_v<V, MultiplayerCore::MoveRejected>) {
          // A well-behaved client never gets here (the same PuzzleCore rules
          // run on both sides); nothing sensible to do but ignore it.
        } else if constexpr (std::is_same_v<V, MultiplayerCore::Finished>) {
          state.phase = Phase::finished;
          state.youWon = value.youWon;
          state.winnerName = value.winnerName;
          state.finalDurationSeconds = value.durationSeconds;
          state.startDate = std::nullopt;
          state.secondsElapsed = value.durationSeconds; // the referee's clock is authoritative
        } else if constexpr (std::is_same_v<V, MultiplayerCore::OpponentLeft>) {
          state.phase = Phase::finished;
          state.youWon = true;
          state.opponentLeft = true;
          state.winnerName = state.playerName;
          state.finalDurationSeconds = state.secondsElapsed;
          state.startDate = std::nullopt;
        }
      },
      message);
}

} // namespace

State initialState(std::string playerName, int gridSize) {
  return State{.playerName = std::move(playerName),
               .gridSize = std::clamp(gridSize, PuzzleCore::minGrid, PuzzleCore::maxGrid)};
}

bool isBoardSolved(const State &state) {
  return !state.tiles.empty() && PuzzleCore::isSolved(state.tiles, state.gridSize);
}

ComposableArchitecture::Feature<State, Action> body() {
  using FeatureStore = ComposableArchitecture::Store<State, Action>;

  return ComposableArchitecture::Update<State, Action>([](State &state, const Action &action,
                                                          FeatureStore &store) {
    std::visit(
        [&](auto &&value) {
          using V = std::decay_t<decltype(value)>;

          if constexpr (std::is_same_v<V, Appeared> || std::is_same_v<V, RematchTapped>) {
            startSession(state, store);
          } else if constexpr (std::is_same_v<V, ClientEvent>) {
            std::visit(
                [&](auto &&event) {
                  using E = std::decay_t<decltype(event)>;
                  if constexpr (std::is_same_v<E, MultiplayerClient::Connected>) {
                    // Still `connecting` until the server queues us.
                  } else if constexpr (std::is_same_v<E, MultiplayerClient::Failed>) {
                    state.phase = Phase::failed;
                    state.startDate = std::nullopt;
                  } else if constexpr (std::is_same_v<E, MultiplayerClient::Closed>) {
                    if (state.phase != Phase::finished) {
                      state.phase = Phase::failed;
                      state.startDate = std::nullopt;
                    }
                  } else if constexpr (std::is_same_v<E, MultiplayerClient::Received>) {
                    handleServerMessage(state, event.message);
                  }
                },
                value.event);
          } else if constexpr (std::is_same_v<V, TileTapped>) {
            if (state.phase == Phase::racing &&
                PuzzleCore::slide(state.tiles, state.moveHistory, state.gridSize, value.index)) {
              // Report the slide; the referee replays it on its copy and
              // relays it to the opponent. The win comes back as
              // `Finished` — never decided locally.
              Dependencies::Dependency<MultiplayerClient::Key> client;
              client->sendMove(value.index);
            }
          } else if constexpr (std::is_same_v<V, TimerTicked>) {
            if (state.phase == Phase::racing && state.startDate.has_value()) {
              Dependencies::Dependency<Dependencies::DateGeneratorKey> date;
              const int seconds = static_cast<int>(date->now() - *state.startDate);
              if (seconds > state.secondsElapsed) {
                state.secondsElapsed = seconds;
              }
            }
          }
        },
        action);
  });
}

} // namespace MultiplayerFeature
