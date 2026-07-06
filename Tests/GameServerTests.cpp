// Tests the multiplayer referee (GameServer::Engine) as pure logic: matchmaking
// deals both players the same seed, moves are re-played (and illegal ones
// rejected) on the server's own boards, the win is detected by the referee —
// never claimed by a client — and a verified result comes out the other end.
// Time and randomness are pinned through the Dependencies library.

import std;
import Dependencies;
import GameServer;
import MultiplayerCore;
import PuzzleCore;
import SharedModels;

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

template <typename Message>
const Message *messageFor(const GameServer::Output &output, GameServer::PlayerId player) {
  for (const auto &outbound : output.messages) {
    if (outbound.player == player) {
      if (const auto *message = std::get_if<Message>(&outbound.message)) {
        return message;
      }
    }
  }
  return nullptr;
}

// The moves that solve a board scrambled with `seed`, derived by replaying the
// scramble and undoing it back-to-front: undoing a slide means sliding the
// same tile back into the cell the hole occupied before that step.
std::vector<int> solutionFor(int grid, std::uint64_t seed) {
  auto rng = Dependencies::RandomNumberGenerator::seeded(seed);
  std::vector<std::string> tiles;
  std::vector<int> history;
  PuzzleCore::scramble(grid, rng, tiles, history, grid * grid * 10);

  std::vector<int> solution;
  solution.reserve(history.size());
  int empty = grid * grid - 1; // the hole starts at the last cell
  std::vector<int> emptyBefore;
  emptyBefore.reserve(history.size());
  for (const int pos : history) {
    emptyBefore.push_back(empty);
    empty = pos;
  }
  for (auto it = emptyBefore.rbegin(); it != emptyBefore.rend(); ++it) {
    solution.push_back(*it);
  }
  return solution;
}

void withPinnedDependencies(const std::function<void()> &body) {
  withDependencies(
      [](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<Dependencies::DateGeneratorKey>(Dependencies::DateGenerator::constant(50.0));
        values.set<Dependencies::RandomNumberGeneratorKey>(
            Dependencies::RandomNumberGenerator::seeded(42));
      },
      [&] {
        body();
        return 0;
      });
}

void testMatchmakingDealsTheSameBoard() {
  withPinnedDependencies([] {
    GameServer::Engine engine;

    const auto queued = engine.join(1, "Ada", 4);
    expect(messageFor<MultiplayerCore::Queued>(queued, 1) != nullptr,
           "matchmaking: first player is queued");

    const auto started = engine.join(2, "Bob", 4);
    const auto *startForAda = messageFor<MultiplayerCore::Start>(started, 1);
    const auto *startForBob = messageFor<MultiplayerCore::Start>(started, 2);
    expect(startForAda != nullptr && startForBob != nullptr, "matchmaking: both players get Start");
    if (startForAda && startForBob) {
      expect(startForAda->seed == startForBob->seed, "matchmaking: same seed for both");
      expect(startForAda->gridSize == 4 && startForBob->gridSize == 4,
             "matchmaking: agreed grid size");
      expect(startForAda->opponentName == "Bob" && startForBob->opponentName == "Ada",
             "matchmaking: opponents introduced by name");
    }
  });
}

void testDifferentBoardSizesDoNotMatch() {
  withPinnedDependencies([] {
    GameServer::Engine engine;
    (void)engine.join(1, "Ada", 4);
    const auto queued = engine.join(2, "Bob", 5);
    expect(messageFor<MultiplayerCore::Queued>(queued, 2) != nullptr,
           "matchmaking: a 5x5 player does not match a 4x4 player");
  });
}

void testMovesAreRefereedAndRelayed() {
  withPinnedDependencies([] {
    GameServer::Engine engine;
    (void)engine.join(1, "Ada", 4);
    const auto started = engine.join(2, "Bob", 4);
    const auto *start = messageFor<MultiplayerCore::Start>(started, 1);
    expect(start != nullptr, "referee: race started");
    if (start == nullptr) {
      return;
    }

    // Reconstruct Ada's board from the seed (exactly what her client does) and
    // pick a legal first move: any neighbor of the hole.
    const auto tiles = PuzzleCore::scrambled(4, start->seed);
    const auto empty = PuzzleCore::emptyIndex(tiles);
    expect(empty.has_value(), "referee: dealt board has a hole");
    const int legal = PuzzleCore::neighbors(*empty, 4).front();

    const auto relayed = engine.move(1, legal);
    const auto *opponentMoved = messageFor<MultiplayerCore::OpponentMoved>(relayed, 2);
    expect(opponentMoved != nullptr, "referee: legal move relayed to the opponent");
    if (opponentMoved) {
      expect(opponentMoved->index == legal && opponentMoved->moveCount == 1,
             "referee: relay carries the move and count");
    }

    // After that slide the hole sits at `legal`, so "moving" that cell again
    // is illegal — the referee rejects rather than trusts it, and the
    // opponent hears nothing.
    const auto rejected = engine.move(1, legal);
    expect(messageFor<MultiplayerCore::MoveRejected>(rejected, 1) != nullptr,
           "referee: illegal move is rejected");
    expect(messageFor<MultiplayerCore::OpponentMoved>(rejected, 2) == nullptr,
           "referee: illegal move is not relayed");
  });
}

void testServerDetectsTheWinAndVerifiesTheResult() {
  withPinnedDependencies([] {
    GameServer::Engine engine;
    (void)engine.join(1, "Ada", 4);
    const auto started = engine.join(2, "Bob", 4);
    const auto *start = messageFor<MultiplayerCore::Start>(started, 1);
    expect(start != nullptr, "win: race started");
    if (start == nullptr) {
      return;
    }

    const std::vector<int> solution = solutionFor(4, start->seed);
    expect(!solution.empty(), "win: scrambled board needs moves");

    GameServer::Output last;
    for (const int move : solution) {
      last = engine.move(1, move);
      expect(messageFor<MultiplayerCore::MoveRejected>(last, 1) == nullptr,
             "win: every solution move is legal on the referee's board");
    }

    const auto *finishedForAda = messageFor<MultiplayerCore::Finished>(last, 1);
    const auto *finishedForBob = messageFor<MultiplayerCore::Finished>(last, 2);
    expect(finishedForAda != nullptr && finishedForAda->youWon, "win: the solver is told they won");
    expect(finishedForBob != nullptr && !finishedForBob->youWon,
           "win: the opponent is told they lost");
    expect(finishedForAda != nullptr && finishedForAda->winnerName == "Ada", "win: winner named");

    expect(last.results.size() == 1, "win: exactly one verified result");
    if (!last.results.empty()) {
      const SharedModels::ScoreSubmission &result = last.results.front();
      expect(result.name == "Ada" && result.gridSize == 4, "win: result names the winner");
      expect(result.moves == static_cast<int>(solution.size()),
             "win: result carries the replayed move count");
      expect(result.duration == 0, "win: pinned clock gives a zero duration");
    }

    // The race is over: further moves are ignored.
    expect(engine.move(2, 0).messages.empty(), "win: moves after the finish are ignored");
  });
}

void testLeavingMidRaceNotifiesTheOpponent() {
  withPinnedDependencies([] {
    GameServer::Engine engine;
    (void)engine.join(1, "Ada", 4);
    (void)engine.join(2, "Bob", 4);

    const auto left = engine.leave(1);
    expect(messageFor<MultiplayerCore::OpponentLeft>(left, 2) != nullptr,
           "leave: opponent is told");
    expect(left.results.empty(), "leave: a walkover records no result");
    expect(engine.leave(2).messages.empty(), "leave: second leave is quiet");

    // Leaving the queue is silent.
    (void)engine.join(3, "Cy", 4);
    expect(engine.leave(3).messages.empty(), "leave: leaving the queue is quiet");
  });
}

} // namespace

int main() {
  testMatchmakingDealsTheSameBoard();
  testDifferentBoardSizesDoNotMatch();
  testMovesAreRefereedAndRelayed();
  testServerDetectsTheWinAndVerifiesTheResult();
  testLeavingMidRaceNotifiesTheOpponent();

  if (failures == 0) {
    std::println("All GameServer tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} GameServer test(s) failed.", failures);
  return 1;
}
