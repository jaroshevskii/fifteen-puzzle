// Tests for the live IDA* solver: scramble a board with legal moves, solve it,
// and verify that applying the returned moves actually reaches the goal.

import std;
import SolverClient;
import SolverClientLive;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

std::vector<std::string> solvedBoard() {
  std::vector<std::string> tiles;
  for (int i = 1; i <= 15; ++i) {
    tiles.push_back(std::to_string(i));
  }
  tiles.push_back("");
  return tiles;
}

int emptyIndex(const std::vector<std::string>& t) {
  return static_cast<int>(std::ranges::distance(t.begin(), std::ranges::find(t, std::string{})));
}

// Scramble by applying `moves` random legal slides, so the result is guaranteed
// solvable (and not absurdly hard for the weighted solver).
std::vector<std::string> scramble(int moves, std::uint64_t seed) {
  auto tiles = solvedBoard();
  std::mt19937_64 rng(seed);
  int empty = emptyIndex(tiles);
  for (int step = 0; step < moves; ++step) {
    const int r = empty / 4, c = empty % 4;
    std::vector<int> neighbors;
    if (r > 0) neighbors.push_back(empty - 4);
    if (r < 3) neighbors.push_back(empty + 4);
    if (c > 0) neighbors.push_back(empty - 1);
    if (c < 3) neighbors.push_back(empty + 1);
    const int pick = neighbors[std::uniform_int_distribution<std::size_t>(0, neighbors.size() - 1)(rng)];
    std::swap(tiles[empty], tiles[pick]);
    empty = pick;
  }
  return tiles;
}

// Apply solver moves (each a tile position adjacent to the empty cell).
void applyMoves(std::vector<std::string>& tiles, const std::vector<int>& moves) {
  for (const int pos : moves) {
    std::swap(tiles[pos], tiles[emptyIndex(tiles)]);
  }
}

void testSolvesScrambledBoards() {
  auto client = SolverClient::live();
  for (std::uint64_t seed : {1ull, 42ull, 1000ull, 77ull}) {
    auto board = scramble(40, seed);
    auto result = client.solve(board, std::stop_token{});
    if (!result.has_value()) {
      expect(false, "solver returned an error for a solvable board");
      continue;
    }
    applyMoves(board, *result);
    expect(board == solvedBoard(), "applying solver moves reaches the goal");
  }
}

}  // namespace

int main() {
  testSolvesScrambledBoards();
  if (failures == 0) {
    std::println("All SolverClient tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} solver test(s) failed.", failures);
  return 1;
}
