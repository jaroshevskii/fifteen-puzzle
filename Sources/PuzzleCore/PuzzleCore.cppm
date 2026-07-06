export module PuzzleCore;

import std;
import Dependencies;

// The pure sliding-puzzle rules, shared by the client (PuzzleFeature,
// MultiplayerFeature) and the server (GameServer) — the analog of isowords'
// `PuzzleGen` target. Because the server re-plays every submitted multiplayer
// move on its own copy of the board, both sides must agree bit-for-bit on the
// shuffle: `scramble` draws indices with a plain modulo of the generator's
// output instead of `std::uniform_int_distribution` (whose mapping is
// implementation-defined and differs between libc++ and MSVC's STL).
export namespace PuzzleCore {

// The playable board sizes. Both the client's Config and the server's request
// validation derive from these, so they can never drift apart.
constexpr int minGrid = 4; // the classic 15-puzzle
constexpr int maxGrid = 13;

inline int rowOf(int index, int grid) { return index / grid; }
inline int colOf(int index, int grid) { return index % grid; }

inline bool isAdjacent(int lhs, int rhs, int grid) {
  const int r1 = rowOf(lhs, grid), c1 = colOf(lhs, grid);
  const int r2 = rowOf(rhs, grid), c2 = colOf(rhs, grid);
  return (r1 == r2 && std::abs(c1 - c2) == 1) || (c1 == c2 && std::abs(r1 - r2) == 1);
}

inline std::vector<std::string> solvedTiles(int grid) {
  const int count = grid * grid;
  std::vector<std::string> tiles;
  tiles.reserve(static_cast<std::size_t>(count));
  for (int i = 1; i < count; ++i) {
    tiles.push_back(std::to_string(i));
  }
  tiles.emplace_back(); // empty cell last
  return tiles;
}

inline bool isSolved(const std::vector<std::string> &tiles, int grid) {
  const int count = grid * grid;
  if (static_cast<int>(tiles.size()) != count) {
    return false;
  }
  for (int i = 0; i < count - 1; ++i) {
    if (tiles[static_cast<std::size_t>(i)] != std::to_string(i + 1)) {
      return false;
    }
  }
  return tiles[static_cast<std::size_t>(count - 1)].empty();
}

inline std::vector<int> neighbors(int pos, int grid) {
  const int r = rowOf(pos, grid), c = colOf(pos, grid);
  std::vector<int> result;
  if (r > 0)
    result.push_back(pos - grid);
  if (r < grid - 1)
    result.push_back(pos + grid);
  if (c > 0)
    result.push_back(pos - 1);
  if (c < grid - 1)
    result.push_back(pos + 1);
  return result;
}

inline std::optional<int> emptyIndex(const std::vector<std::string> &tiles) {
  const auto it = std::ranges::find(tiles, std::string{});
  if (it == tiles.end()) {
    return std::nullopt;
  }
  return static_cast<int>(std::ranges::distance(tiles.begin(), it));
}

// Slides the tile at `pos` into the empty cell if they are adjacent, recording
// the move in `history`. Returns whether the board changed. This is the single
// move rule: the client applies it on taps, and the server applies it to its
// own copy of each multiplayer board to validate what clients report.
inline bool slide(std::vector<std::string> &tiles, std::vector<int> &history, int grid, int pos) {
  const auto empty = emptyIndex(tiles);
  if (empty.has_value() && pos >= 0 && pos < static_cast<int>(tiles.size()) &&
      isAdjacent(pos, *empty, grid)) {
    std::swap(tiles[static_cast<std::size_t>(pos)], tiles[static_cast<std::size_t>(*empty)]);
    history.push_back(pos);
    return true;
  }
  return false;
}

// Uniform-enough index in [0, count) that is identical on every platform
// (bias from the modulo is irrelevant for shuffling, determinism is not).
inline std::size_t nextIndex(Dependencies::RandomNumberGenerator &rng, std::size_t count) {
  return static_cast<std::size_t>(rng() % count);
}

// Builds a board by applying `count` random legal slides from solved, recording
// the move history (so the board is always solvable and reversible).
inline void scramble(int grid, Dependencies::RandomNumberGenerator &rng,
                     std::vector<std::string> &tiles, std::vector<int> &history, int count) {
  tiles = solvedTiles(grid);
  history.clear();
  int empty = grid * grid - 1;
  int previous = -1;
  for (int i = 0; i < count; ++i) {
    auto options = neighbors(empty, grid);
    std::erase(options, previous); // avoid immediately undoing the last slide
    const int pick = options[nextIndex(rng, options.size())];
    std::swap(tiles[static_cast<std::size_t>(empty)], tiles[static_cast<std::size_t>(pick)]);
    history.push_back(pick);
    previous = empty;
    empty = pick;
  }
  if (isSolved(tiles, grid) && !neighbors(empty, grid).empty()) {
    const int pick = neighbors(empty, grid).front();
    std::swap(tiles[static_cast<std::size_t>(empty)], tiles[static_cast<std::size_t>(pick)]);
    history.push_back(pick);
  }
}

// The multiplayer deal: both players (and the server's referee copies) build
// the same board from the room's seed.
inline std::vector<std::string> scrambled(int grid, std::uint64_t seed) {
  auto rng = Dependencies::RandomNumberGenerator::seeded(seed);
  std::vector<std::string> tiles;
  std::vector<int> history;
  scramble(grid, rng, tiles, history, grid * grid * 10);
  return tiles;
}

} // namespace PuzzleCore
