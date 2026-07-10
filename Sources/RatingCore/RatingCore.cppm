export module RatingCore;

import std;

// Elo ratings for head-to-head races, defined once and shared by both sides —
// the client shows the projected ±Δ before a match, the server applies the
// identical function when a verified result comes in, so the two can never
// disagree (the same single-definition discipline as PuzzleCore/ServerRouter).
// Pure and dependency-free, so it is exhaustively property-testable.
export namespace RatingCore {

// Every player starts here; the classic chess anchor.
constexpr int startingRating = 1200;

// K-factor decays with experience: provisional players move fast, veterans
// settle — the standard USCF-style schedule.
constexpr int kFactor(int gamesPlayed) {
  if (gamesPlayed < 10) {
    return 40; // provisional
  }
  if (gamesPlayed < 30) {
    return 20;
  }
  return 10; // established
}

// Expected score for `rating` against `opponentRating` — the logistic curve,
// in [0, 1] (0.5 at equal ratings).
constexpr double expectedScore(int rating, int opponentRating) {
  return 1.0 / (1.0 + std::pow(10.0, static_cast<double>(opponentRating - rating) / 400.0));
}

// The rating delta a player receives for a game whose actual `score` is 1.0
// (win), 0.5 (draw) or 0.0 (loss). Rounded to the nearest integer, away from
// zero, so a win is always worth at least ±1.
constexpr int ratingDelta(int rating, int opponentRating, double score, int gamesPlayed) {
  const double raw =
      static_cast<double>(kFactor(gamesPlayed)) * (score - expectedScore(rating, opponentRating));
  return static_cast<int>(raw >= 0.0 ? std::floor(raw + 0.5) : std::ceil(raw - 0.5));
}

// A player's rating record.
struct Rating {
  int value = startingRating;
  int gamesPlayed = 0;

  bool operator==(const Rating &) const = default;
};

// The result of applying one game to two players. `winnerDelta` is always >= 0
// and `loserDelta` <= 0 (Elo is very nearly zero-sum; it differs only when the
// two K-factors differ).
struct Outcome {
  Rating winner;
  Rating loser;
  int winnerDelta = 0;
  int loserDelta = 0;

  bool operator==(const Outcome &) const = default;
};

// Applies a decisive game (no draws in a race) and returns both updated
// records. Both players' games-played counter advances.
constexpr Outcome applyWin(Rating winner, Rating loser) {
  const int winnerDelta = ratingDelta(winner.value, loser.value, 1.0, winner.gamesPlayed);
  const int loserDelta = ratingDelta(loser.value, winner.value, 0.0, loser.gamesPlayed);
  return Outcome{
      .winner = Rating{.value = winner.value + winnerDelta, .gamesPlayed = winner.gamesPlayed + 1},
      .loser = Rating{.value = loser.value + loserDelta, .gamesPlayed = loser.gamesPlayed + 1},
      .winnerDelta = winnerDelta,
      .loserDelta = loserDelta};
}

// The rating change a player *would* get for beating / losing to an opponent —
// for the pre-match "+18 / −18" projection shown in the UI.
struct Projection {
  int ifWin = 0;
  int ifLoss = 0;

  bool operator==(const Projection &) const = default;
};

constexpr Projection project(Rating player, int opponentRating) {
  return Projection{.ifWin = ratingDelta(player.value, opponentRating, 1.0, player.gamesPlayed),
                    .ifLoss = ratingDelta(player.value, opponentRating, 0.0, player.gamesPlayed)};
}

// Seasonal ranks by rating threshold, lowest-first.
enum class Rank : std::uint8_t { bronze, silver, gold, platinum, diamond, master, grandmaster };

constexpr Rank rankFor(int rating) {
  if (rating < 1000) {
    return Rank::bronze;
  }
  if (rating < 1200) {
    return Rank::silver;
  }
  if (rating < 1400) {
    return Rank::gold;
  }
  if (rating < 1600) {
    return Rank::platinum;
  }
  if (rating < 1800) {
    return Rank::diamond;
  }
  if (rating < 2000) {
    return Rank::master;
  }
  return Rank::grandmaster;
}

constexpr std::string_view rankName(Rank rank) {
  switch (rank) {
  case Rank::bronze:
    return "Bronze";
  case Rank::silver:
    return "Silver";
  case Rank::gold:
    return "Gold";
  case Rank::platinum:
    return "Platinum";
  case Rank::diamond:
    return "Diamond";
  case Rank::master:
    return "Master";
  case Rank::grandmaster:
    return "Grandmaster";
  }
  return "Bronze";
}

// A soft-reset for a new season: pull ratings toward the anchor by a third
// (keeps skill order, compresses the spread) and zero the game counters so the
// provisional K-factor lets players re-settle quickly.
constexpr Rating softReset(Rating rating) {
  return Rating{.value = startingRating + (rating.value - startingRating) * 2 / 3,
                .gamesPlayed = 0};
}

} // namespace RatingCore
