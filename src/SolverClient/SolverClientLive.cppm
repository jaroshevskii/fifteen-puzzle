export module SolverClientLive;

import std;
import SolverClient;

// Live solver: weighted IDA* (iterative-deepening A*) with a Manhattan-distance
// heuristic computed over a `std::mdspan` 4x4 view of the board. It is a pure,
// CPU-bound computation that periodically checks its `std::stop_token`, so the
// effect layer can run it on a background thread and cancel it instantly.
export namespace SolverClient {

Client live();

}  // namespace SolverClient

namespace SolverClient {

namespace {

constexpr int kSide = 4;
constexpr int kCells = kSide * kSide;
constexpr int kFound = -1;
constexpr int kWeight = 3;                  // >1: suboptimal but fast — great for an animated solve
constexpr long long kNodeBudget = 30'000'000;

using Board = std::array<int, kCells>;

// Sum of Manhattan distances of every tile from its goal, viewing the flat
// board as a 4x4 grid through std::mdspan.
int manhattan(const Board& cells) {
  std::mdspan grid(cells.data(), kSide, kSide);
  int total = 0;
  for (int row = 0; row < kSide; ++row) {
    for (int col = 0; col < kSide; ++col) {
      const int value = grid[row, col];
      if (value == 0) {
        continue;
      }
      const int goalRow = (value - 1) / kSide;
      const int goalCol = (value - 1) % kSide;
      total += std::abs(row - goalRow) + std::abs(col - goalCol);
    }
  }
  return total;
}

bool isSolvable(const Board& cells) {
  int inversions = 0;
  int emptyRowFromBottom = 0;
  for (int i = 0; i < kCells; ++i) {
    if (cells[i] == 0) {
      emptyRowFromBottom = kSide - (i / kSide);
      continue;
    }
    for (int j = i + 1; j < kCells; ++j) {
      if (cells[j] != 0 && cells[i] > cells[j]) {
        ++inversions;
      }
    }
  }
  return ((inversions + emptyRowFromBottom) % 2) == 1;
}

struct Search {
  std::stop_token stop;
  long long nodes = 0;
  bool cancelled = false;
  std::vector<int> path;  // tile positions to tap, in order

  // Depth-first bounded search. Returns kFound, or the smallest f-cost that
  // exceeded `bound` (for the next iteration), or INT_MAX when exhausted.
  int dfs(Board& cells, int empty, int g, int bound, int cameFrom) {
    if (cancelled) {
      return std::numeric_limits<int>::max();
    }
    if ((++nodes & 0x1FFF) == 0 && stop.stop_requested()) {
      cancelled = true;
      return std::numeric_limits<int>::max();
    }
    if (nodes > kNodeBudget) {
      cancelled = true;  // give up; reported as unsolvable
      return std::numeric_limits<int>::max();
    }

    const int h = manhattan(cells);
    if (h == 0) {
      return kFound;
    }
    const int f = g + kWeight * h;
    if (f > bound) {
      return f;
    }

    int best = std::numeric_limits<int>::max();
    const int row = empty / kSide;
    const int col = empty % kSide;
    constexpr int deltaRow[4]{-1, 1, 0, 0};
    constexpr int deltaCol[4]{0, 0, -1, 1};

    for (int dir = 0; dir < 4; ++dir) {
      const int nr = row + deltaRow[dir];
      const int nc = col + deltaCol[dir];
      if (nr < 0 || nr >= kSide || nc < 0 || nc >= kSide) {
        continue;
      }
      const int neighbor = nr * kSide + nc;
      if (neighbor == cameFrom) {
        continue;  // don't immediately undo the previous slide
      }

      std::swap(cells[empty], cells[neighbor]);
      path.push_back(neighbor);
      const int t = dfs(cells, neighbor, g + 1, bound, empty);
      if (t == kFound) {
        return kFound;  // leave path intact: it is the solution
      }
      path.pop_back();
      std::swap(cells[empty], cells[neighbor]);
      if (cancelled) {
        return std::numeric_limits<int>::max();
      }
      best = std::min(best, t);
    }
    return best;
  }
};

std::expected<std::vector<int>, SolveError> solveBoard(std::vector<std::string> tiles, std::stop_token stop) {
  if (static_cast<int>(tiles.size()) != kCells) {
    return std::unexpected(SolveError::unsolvable);
  }

  Board cells{};
  int empty = 0;
  for (int i = 0; i < kCells; ++i) {
    if (tiles[i].empty()) {
      cells[i] = 0;
      empty = i;
    } else {
      cells[i] = std::stoi(tiles[i]);
    }
  }

  if (!isSolvable(cells)) {
    return std::unexpected(SolveError::unsolvable);
  }

  Search search{.stop = stop};
  int bound = kWeight * manhattan(cells);
  while (true) {
    const int t = search.dfs(cells, empty, 0, bound, -1);
    if (search.cancelled) {
      return std::unexpected(stop.stop_requested() ? SolveError::cancelled : SolveError::unsolvable);
    }
    if (t == kFound) {
      return search.path;
    }
    if (t == std::numeric_limits<int>::max()) {
      return std::unexpected(SolveError::unsolvable);
    }
    bound = t;
  }
}

}  // namespace

Client live() {
  return Client{.solve = [](std::vector<std::string> tiles, std::stop_token stop) {
    return solveBoard(std::move(tiles), std::move(stop));
  }};
}

}  // namespace SolverClient
