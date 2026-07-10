module LiveFeature; // implementation unit

import std;
import ComposableArchitecture;
import Dependencies;
import MultiplayerClient;
import MultiplayerCore;

namespace LiveFeature {

namespace {

void handleServerMessage(State &state, const MultiplayerCore::ServerMessage &message) {
  std::visit(
      [&](auto &&value) {
        using V = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<V, MultiplayerCore::Presence>) {
          state.online = value.online;
          state.racing = value.racing;
          state.waiting = value.waiting;
        } else if constexpr (std::is_same_v<V, MultiplayerCore::MatchStarted>) {
          // De-dupe by id (the snapshot on subscribe may repeat one we know).
          if (std::ranges::none_of(state.matches,
                                   [&](const Match &m) { return m.matchId == value.matchId; })) {
            state.matches.insert(state.matches.begin(), Match{.matchId = value.matchId,
                                                              .gridSize = value.gridSize,
                                                              .playerA = value.playerA,
                                                              .playerB = value.playerB});
          }
        } else if constexpr (std::is_same_v<V, MultiplayerCore::MatchEnded>) {
          std::erase_if(state.matches, [&](const Match &m) { return m.matchId == value.matchId; });
          state.recent.insert(state.recent.begin(),
                              FinishedMatch{.winnerName = value.winnerName,
                                            .gridSize = value.gridSize,
                                            .durationSeconds = value.durationSeconds});
          if (state.recent.size() > maxRecent) {
            state.recent.resize(maxRecent);
          }
        }
        // Other server messages never reach an observer connection.
      },
      message);
}

} // namespace

State initialState() { return State{}; }

ComposableArchitecture::Feature<State, Action> body() {
  using FeatureStore = ComposableArchitecture::Store<State, Action>;

  return ComposableArchitecture::Update<State, Action>(
      [](State &state, const Action &action, FeatureStore &store) {
        std::visit(
            [&](auto &&value) {
              using V = std::decay_t<decltype(value)>;

              if constexpr (std::is_same_v<V, Appeared>) {
                state = State{}; // reset on each open
                store.addTask(
                    [](FeatureStore &store, std::stop_token stop) {
                      Dependencies::Dependency<MultiplayerClient::Key> client;
                      client->observe(
                          [&store](MultiplayerClient::Event event) {
                            store.send(ClientEvent{std::move(event)});
                          },
                          std::move(stop));
                    },
                    std::string(kConnectionCancelId));
              } else if constexpr (std::is_same_v<V, ClientEvent>) {
                std::visit(
                    [&](auto &&event) {
                      using E = std::decay_t<decltype(event)>;
                      if constexpr (std::is_same_v<E, MultiplayerClient::Connected>) {
                        state.phase = Phase::live;
                      } else if constexpr (std::is_same_v<E, MultiplayerClient::Failed> ||
                                           std::is_same_v<E, MultiplayerClient::Closed>) {
                        state.phase = Phase::failed;
                      } else if constexpr (std::is_same_v<E, MultiplayerClient::Received>) {
                        handleServerMessage(state, event.message);
                      }
                    },
                    value.event);
              }
            },
            action);
      });
}

} // namespace LiveFeature
