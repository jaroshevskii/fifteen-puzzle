export module SavedGame;

import std;

// The persisted snapshot of an in-progress game, used to resume after the app
// is closed. `tiles` captures the exact board (no replay needed); `moveHistory`
// is kept too because the auto-solver plans by reversing it. Wall-clock time is
// NOT stored — elapsed time is restored by back-dating `startDate` from
// `secondsElapsed`.
export namespace SavedGame {

struct Game {
  int grid = 4;
  std::vector<std::string> tiles;
  std::vector<int> moveHistory;
  int secondsElapsed = 0;

  bool operator==(const Game &) const = default;
};

} // namespace SavedGame
