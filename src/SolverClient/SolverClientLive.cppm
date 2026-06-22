export module SolverClientLive;

import std;
import SolverClient;

// Live planner. A board reached from solved by the moves `h0, h1, ..., h(k-1)`
// is returned to solved by the inverse sequence `h(k-2), ..., h0, blank` — undo
// every slide in reverse, finishing by sliding the blank home. This is O(k) and
// has nothing to do with board size, so it scales to any N×N instantly. Honors
// the stop token (a pathological history could be huge).
export namespace SolverClient {

Client live();

} // namespace SolverClient

namespace SolverClient {

Client live() {
  return Client{
      .plan = [](std::vector<int> history, int gridSize, std::stop_token stop)
          -> std::expected<std::vector<int>, SolveError> {
        std::vector<int> solution;
        if (history.empty()) {
          return solution; // already solved
        }
        solution.reserve(history.size());
        for (int i = static_cast<int>(history.size()) - 2; i >= 0; --i) {
          if ((i & 0xFFFF) == 0 && stop.stop_requested()) {
            return std::unexpected(SolveError::cancelled);
          }
          solution.push_back(history[i]);
        }
        solution.push_back(gridSize * gridSize -
                           1); // slide the blank back to the corner
        return solution;
      }};
}

} // namespace SolverClient
