export module SolverClient;

import std;
import Dependencies;

// Interface for the puzzle-solver dependency. Given a board, it computes a
// sequence of tile positions to slide that drives the board to solved, honoring
// a `std::stop_token` for cooperative cancellation. The live IDA* implementation
// lives in `SolverClientLive`; tests inject a deterministic stub.
export namespace SolverClient {

enum class SolveError {
  unsolvable,
  cancelled,
};

struct Client {
  // tiles: board labels ("" is the empty cell). Returns the ordered positions to
  // tap (each adjacent to the empty cell at that step), or an error.
  std::function<std::expected<std::vector<int>, SolveError>(std::vector<std::string>, std::stop_token)>
      solve = [](std::vector<std::string>, std::stop_token) {
        return std::expected<std::vector<int>, SolveError>{std::unexpect, SolveError::unsolvable};
      };
};

struct Key : Dependencies::DependencyKey<Key, Client> {
  // No-op by default; the real solver is supplied by `SolverClientLive`.
  static Client liveValue() { return Client{}; }
};

}  // namespace SolverClient
