export module SolverClient;

import std;
import Dependencies;

// Interface for the auto-solve planner. The game produces every board by legal
// slides from the solved state and records that move history, so a solution is
// simply the inverse of that history — O(moves) and independent of board size,
// which is why auto-solve scales to arbitrarily large boards with no search.
// The live planner lives in `SolverClientLive`; tests inject a stub.
export namespace SolverClient {

enum class SolveError {
  cancelled,
};

struct Client {
  // history: tile positions tapped (from solved) that produced the board.
  // gridSize: board side length. Returns the positions to tap to reach solved.
  std::function<std::expected<std::vector<int>, SolveError>(std::vector<int>, int, std::stop_token)> plan =
      [](std::vector<int>, int, std::stop_token) {
        return std::expected<std::vector<int>, SolveError>{std::in_place};
      };
};

struct Key : Dependencies::DependencyKey<Key, Client> {
  static Client liveValue() { return Client{}; }  // real planner supplied by SolverClientLive
};

}  // namespace SolverClient
