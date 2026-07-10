// Tests the shared Elo rating core: symmetry of expected scores, the direction
// and boundedness of rating changes, the K-factor schedule, near-zero-sum
// conservation, rank thresholds, and the seasonal soft-reset. Pure functions,
// so these are plain property-style checks.

import std;
import RatingCore;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

bool approx(double a, double b) { return std::abs(a - b) < 1e-9; }

void testExpectedScoreSymmetry() {
  expect(approx(RatingCore::expectedScore(1200, 1200), 0.5),
         "expected score is 0.5 at equal ratings");
  const double higher = RatingCore::expectedScore(1400, 1200);
  const double lower = RatingCore::expectedScore(1200, 1400);
  expect(higher > 0.5 && lower < 0.5, "favourite expected above 0.5, underdog below");
  expect(approx(higher + lower, 1.0), "expected scores of the two sides sum to 1");
}

void testWinDirectionAndBounds() {
  const RatingCore::Rating even{.value = 1200, .gamesPlayed = 50};
  const auto outcome = RatingCore::applyWin(even, even);
  expect(outcome.winnerDelta > 0, "winner gains rating");
  expect(outcome.loserDelta < 0, "loser loses rating");
  expect(outcome.winner.value > 1200 && outcome.loser.value < 1200, "ratings move apart");
  expect(outcome.winner.gamesPlayed == 51 && outcome.loser.gamesPlayed == 51,
         "both games-played counters advance");
  // Established K=10: an even game moves exactly K*(1-0.5)=5 each way.
  expect(outcome.winnerDelta == 5 && outcome.loserDelta == -5, "even established game is worth ±5");
}

void testUpsetVsExpectedWin() {
  const RatingCore::Rating favourite{.value = 1600, .gamesPlayed = 50};
  const RatingCore::Rating underdog{.value = 1200, .gamesPlayed = 50};
  const int underdogWins = RatingCore::applyWin(underdog, favourite).winnerDelta;
  const int favouriteWins = RatingCore::applyWin(favourite, underdog).winnerDelta;
  expect(underdogWins > favouriteWins, "beating a stronger player is worth more");
  expect(favouriteWins >= 1, "even an expected win is worth at least 1");
}

void testKFactorSchedule() {
  expect(RatingCore::kFactor(0) == 40 && RatingCore::kFactor(9) == 40, "provisional K = 40");
  expect(RatingCore::kFactor(10) == 20 && RatingCore::kFactor(29) == 20, "mid K = 20");
  expect(RatingCore::kFactor(30) == 10, "established K = 10");

  const RatingCore::Rating fresh{.value = 1200, .gamesPlayed = 0};
  const RatingCore::Rating veteran{.value = 1200, .gamesPlayed = 50};
  expect(RatingCore::applyWin(fresh, fresh).winnerDelta >
             RatingCore::applyWin(veteran, veteran).winnerDelta,
         "provisional players move faster than veterans");
}

void testProjectionMatchesApply() {
  const RatingCore::Rating player{.value = 1300, .gamesPlayed = 15};
  const RatingCore::Rating opponent{.value = 1450, .gamesPlayed = 40};
  const auto projection = RatingCore::project(player, opponent.value);
  const auto win = RatingCore::applyWin(player, opponent);
  const auto loss = RatingCore::applyWin(opponent, player);
  expect(projection.ifWin == win.winnerDelta, "projected win delta matches applyWin");
  expect(projection.ifLoss == loss.loserDelta, "projected loss delta matches applyWin");
  expect(projection.ifWin > 0 && projection.ifLoss < 0, "projection has the right signs");
}

void testRanksAndReset() {
  expect(RatingCore::rankFor(900) == RatingCore::Rank::bronze, "sub-1000 is Bronze");
  expect(RatingCore::rankFor(1200) == RatingCore::Rank::gold, "1200 is Gold");
  expect(RatingCore::rankFor(2100) == RatingCore::Rank::grandmaster, "2000+ is Grandmaster");
  expect(RatingCore::rankName(RatingCore::rankFor(1700)) == "Diamond", "1700 names Diamond");

  const RatingCore::Rating high{.value = 1800, .gamesPlayed = 60};
  const auto reset = RatingCore::softReset(high);
  expect(reset.value < high.value && reset.value > RatingCore::startingRating,
         "soft reset pulls toward the anchor but keeps order");
  expect(reset.gamesPlayed == 0, "soft reset clears the games counter");
  expect(RatingCore::softReset(RatingCore::Rating{.value = 1200}).value == 1200,
         "a player at the anchor is unchanged by a reset");
}

} // namespace

int main() {
  testExpectedScoreSymmetry();
  testWinDirectionAndBounds();
  testUpsetVsExpectedWin();
  testKFactorSchedule();
  testProjectionMatchesApply();
  testRanksAndReset();

  if (failures == 0) {
    std::println("All RatingCore tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} RatingCore test(s) failed.", failures);
  return 1;
}
