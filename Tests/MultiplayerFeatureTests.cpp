// Tests MultiplayerFeature with the TestStore and a scripted connection stub.
// Presenting the screen dispatches Appeared, whose connection task (run inline
// here) streams the scripted server events back as ClientEvent actions —
// covering the queue → race → finish flow, board dealing from the seed, move
// reporting, and the failure paths.

import std;
import ComposableArchitecture;
import MultiplayerClient;
import MultiplayerCore;
import MultiplayerFeature;
import PuzzleCore;

using ComposableArchitecture::TestStore;
using Dependencies::DependencyContext;
using Dependencies::DependencyValues;
using Dependencies::withDependencies;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

constexpr std::uint64_t kSeed = 7;

// A client whose connection immediately streams the given events, and records
// every reported move.
MultiplayerClient::Client stubClient(std::vector<MultiplayerClient::Event> events,
                                     std::shared_ptr<std::vector<int>> reportedMoves = nullptr) {
  MultiplayerClient::Client client;
  client.connect = [events](std::string, int, std::function<void(MultiplayerClient::Event)> onEvent,
                            std::stop_token) {
    for (const auto &event : events) {
      onEvent(event);
    }
  };
  if (reportedMoves) {
    client.sendMove = [reportedMoves](int index) { reportedMoves->push_back(index); };
  }
  return client;
}

MultiplayerClient::Event received(MultiplayerCore::ServerMessage message) {
  return MultiplayerClient::Received{std::move(message)};
}

void testQueueRaceAndFinish() {
  const auto moves = std::make_shared<std::vector<int>>();

  // A legal first move for the opponent on the dealt board — the server only
  // ever relays validated moves, so the script does the same.
  const auto dealtBoard = PuzzleCore::scrambled(4, kSeed);
  const int opponentMove = PuzzleCore::neighbors(*PuzzleCore::emptyIndex(dealtBoard), 4).front();

  withDependencies(
      [&](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<MultiplayerClient::Key>(stubClient(
            {
                MultiplayerClient::Connected{},
                received(MultiplayerCore::Queued{}),
                received(
                    MultiplayerCore::Start{.seed = kSeed, .gridSize = 4, .opponentName = "Bob"}),
                received(MultiplayerCore::OpponentMoved{.index = opponentMove, .moveCount = 1}),
            },
            moves));
      },
      [&] {
        TestStore<MultiplayerFeature::State, MultiplayerFeature::Action> store(
            MultiplayerFeature::initialState("Ada", 4), MultiplayerFeature::body);

        // Presenting dispatches Appeared (the parent's job), which starts the
        // connection; the stub streams its events straight back.
        store.send(MultiplayerFeature::Appeared{}, {});

        store.receive({}); // Connected — still just `connecting`
        store.receive([](MultiplayerFeature::State &state) {
          state.phase = MultiplayerFeature::Phase::queued;
        });
        const auto dealt = PuzzleCore::scrambled(4, kSeed);
        store.receive([&](MultiplayerFeature::State &state) {
          state.phase = MultiplayerFeature::Phase::racing;
          state.opponentName = "Bob";
          state.tiles = dealt;
          state.opponentTiles = dealt; // preview starts from the same deal
          state.startDate = 0.0;       // the test clock is pinned at 0
        });
        store.receive([&](MultiplayerFeature::State &state) {
          state.opponentMoveCount = 1;
          // The relayed move is replayed on the preview board.
          std::vector<int> ignored;
          PuzzleCore::slide(state.opponentTiles, ignored, 4, opponentMove);
        });
        expect(store.state().opponentTiles != dealt,
               "race: opponent preview board tracks their moves");

        expect(!store.failed(), "race: state transitions match");
        expect(store.state().tiles == PuzzleCore::scrambled(4, kSeed),
               "race: board dealt deterministically from the seed");

        // A legal slide moves a tile locally and reports it to the referee.
        const auto empty = PuzzleCore::emptyIndex(store.state().tiles);
        expect(empty.has_value(), "race: dealt board has a hole");
        const int legal = PuzzleCore::neighbors(*empty, 4).front();
        store.send(MultiplayerFeature::TileTapped{legal}, [&](MultiplayerFeature::State &state) {
          PuzzleCore::slide(state.tiles, state.moveHistory, state.gridSize, legal);
        });
        expect(moves->size() == 1 && moves->front() == legal,
               "race: the slide was reported to the server");

        // An illegal tap (the hole itself) changes nothing and reports nothing.
        const auto holeNow = PuzzleCore::emptyIndex(store.state().tiles);
        store.send(MultiplayerFeature::TileTapped{*holeNow}, {});
        expect(moves->size() == 1, "race: illegal tap not reported");

        // The referee alone ends the race.
        store.send(MultiplayerFeature::ClientEvent{received(MultiplayerCore::Finished{
                       .youWon = true, .winnerName = "Ada", .durationSeconds = 42, .moves = 10})},
                   [](MultiplayerFeature::State &state) {
                     state.phase = MultiplayerFeature::Phase::finished;
                     state.youWon = true;
                     state.winnerName = "Ada";
                     state.finalDurationSeconds = 42;
                     state.secondsElapsed = 42;
                     state.startDate = std::nullopt;
                   });

        expect(!store.failed(), "race: finish transition matches");
        return 0;
      });
}

void testConnectionFailure() {
  withDependencies(
      [](DependencyValues &values) {
        values.context = DependencyContext::test;
        // MultiplayerClient::Key's test default fails immediately — the path a
        // test hits when it forgets to stub the client, and the path the app
        // hits with no server running.
      },
      [] {
        TestStore<MultiplayerFeature::State, MultiplayerFeature::Action> store(
            MultiplayerFeature::initialState("Ada", 4), MultiplayerFeature::body);

        store.send(MultiplayerFeature::Appeared{}, {});
        store.receive([](MultiplayerFeature::State &state) {
          state.phase = MultiplayerFeature::Phase::failed;
        });

        expect(!store.failed(), "failure: connect failure surfaces");
        expect(store.state().phase == MultiplayerFeature::Phase::failed,
               "failure: phase is failed");
        return 0;
      });
}

void testOpponentLeavingWinsTheRace() {
  withDependencies(
      [](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<MultiplayerClient::Key>(stubClient({
            MultiplayerClient::Connected{},
            received(MultiplayerCore::Queued{}),
            received(MultiplayerCore::Start{.seed = kSeed, .gridSize = 4, .opponentName = "Bob"}),
            received(MultiplayerCore::OpponentLeft{}),
        }));
      },
      [] {
        TestStore<MultiplayerFeature::State, MultiplayerFeature::Action> store(
            MultiplayerFeature::initialState("Ada", 4), MultiplayerFeature::body);

        store.send(MultiplayerFeature::Appeared{}, {});
        store.receive({}); // Connected
        store.receive([](MultiplayerFeature::State &state) {
          state.phase = MultiplayerFeature::Phase::queued;
        });
        store.receive([](MultiplayerFeature::State &state) {
          state.phase = MultiplayerFeature::Phase::racing;
          state.opponentName = "Bob";
          state.tiles = PuzzleCore::scrambled(4, kSeed);
          state.opponentTiles = state.tiles;
          state.startDate = 0.0;
        });
        store.receive([](MultiplayerFeature::State &state) {
          state.phase = MultiplayerFeature::Phase::finished;
          state.youWon = true;
          state.opponentLeft = true;
          state.winnerName = "Ada";
          state.startDate = std::nullopt;
        });

        expect(!store.failed(), "walkover: state transitions match");
        return 0;
      });
}

void testConnectionLostMidRaceFails() {
  withDependencies(
      [](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<MultiplayerClient::Key>(stubClient({
            MultiplayerClient::Connected{},
            received(MultiplayerCore::Queued{}),
            received(MultiplayerCore::Start{.seed = kSeed, .gridSize = 4, .opponentName = "Bob"}),
            MultiplayerClient::Closed{},
        }));
      },
      [] {
        TestStore<MultiplayerFeature::State, MultiplayerFeature::Action> store(
            MultiplayerFeature::initialState("Ada", 4), MultiplayerFeature::body);

        store.send(MultiplayerFeature::Appeared{}, {});
        store.receive({}); // Connected
        store.receive([](MultiplayerFeature::State &state) {
          state.phase = MultiplayerFeature::Phase::queued;
        });
        store.receive([](MultiplayerFeature::State &state) {
          state.phase = MultiplayerFeature::Phase::racing;
          state.opponentName = "Bob";
          state.tiles = PuzzleCore::scrambled(4, kSeed);
          state.opponentTiles = state.tiles;
          state.startDate = 0.0;
        });
        store.receive([](MultiplayerFeature::State &state) {
          state.phase = MultiplayerFeature::Phase::failed;
          state.startDate = std::nullopt;
        });

        expect(!store.failed(), "drop: state transitions match");
        return 0;
      });
}

} // namespace

int main() {
  testQueueRaceAndFinish();
  testConnectionFailure();
  testOpponentLeavingWinsTheRace();
  testConnectionLostMidRaceFails();

  if (failures == 0) {
    std::println("All MultiplayerFeature tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} MultiplayerFeature test(s) failed.", failures);
  return 1;
}
