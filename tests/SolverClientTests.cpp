// Tests the reverse-history planner across board sizes: scramble with recorded
// legal moves, plan the inverse, apply it, and verify the board reaches solved.

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

std::vector<std::string> solvedBoard(int n) {
  std::vector<std::string> tiles;
  for (int i = 1; i < n * n; ++i) {
    tiles.push_back(std::to_string(i));
  }
  tiles.push_back("");
  return tiles;
}

int emptyIndex(const std::vector<std::string> &t) {
  return static_cast<int>(
      std::ranges::distance(t.begin(), std::ranges::find(t, std::string{})));
}

struct Scramble {
  std::vector<std::string> tiles;
  std::vector<int> history;
};

Scramble scramble(int n, int moves, std::uint64_t seed) {
  Scramble s{solvedBoard(n), {}};
  std::mt19937_64 rng(seed);
  int empty = emptyIndex(s.tiles);
  for (int step = 0; step < moves; ++step) {
    const int r = empty / n, c = empty % n;
    std::vector<int> nb;
    if (r > 0)
      nb.push_back(empty - n);
    if (r < n - 1)
      nb.push_back(empty + n);
    if (c > 0)
      nb.push_back(empty - 1);
    if (c < n - 1)
      nb.push_back(empty + 1);
    const int pick =
        nb[std::uniform_int_distribution<std::size_t>(0, nb.size() - 1)(rng)];
    std::swap(s.tiles[empty], s.tiles[pick]);
    s.history.push_back(pick);
    empty = pick;
  }
  return s;
}

void applyMoves(std::vector<std::string> &tiles,
                const std::vector<int> &moves) {
  for (const int pos : moves) {
    std::swap(tiles[pos], tiles[emptyIndex(tiles)]);
  }
}

void testPlansAllSizes() {
  auto client = SolverClient::live();
  long long longest = 0;
  for (int n = 4; n <= 13; ++n) { // levels 0..9
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
      auto s = scramble(n, n * n * 10, seed);
      auto plan = client.plan(s.history, n, std::stop_token{});
      if (!plan.has_value()) {
        expect(false,
               std::format("n={} seed={}: planner returned an error", n, seed));
        continue;
      }
      longest =
          std::max<long long>(longest, static_cast<long long>(plan->size()));
      applyMoves(s.tiles, *plan);
      expect(s.tiles == solvedBoard(n),
             std::format("n={} seed={}: did not reach goal", n, seed));
    }
  }
  std::println("planned 4..13 x 50 scrambles; longest plan = {} moves",
               longest);
}

} // namespace

int main() {
  const auto start = std::chrono::steady_clock::now();
  testPlansAllSizes();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  if (failures == 0) {
    std::println("All SolverClient tests passed in {} ms.", ms.count());
    return 0;
  }
  std::println(std::cerr, "{} planner test(s) failed.", failures);
  return 1;
}
