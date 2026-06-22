export module Dependencies:RandomNumberGenerator;

import std;
import :Core;

// A controllable source of randomness, mirroring TCA's
// `\.withRandomNumberGenerator` dependency. It satisfies the standard
// `UniformRandomBitGenerator` requirements, so it can be handed directly to
// `std::shuffle`, `std::uniform_int_distribution`, etc. Routing randomness
// through this dependency makes shuffling deterministic under test.
export namespace Dependencies {

struct RandomNumberGenerator {
  using result_type = std::uint64_t;

  std::function<result_type()> next;

  static constexpr result_type min() noexcept { return 0; }
  static constexpr result_type max() noexcept {
    return std::numeric_limits<result_type>::max();
  }
  result_type operator()() { return next(); }

  // A generator seeded from a fixed value, producing a repeatable sequence.
  static RandomNumberGenerator seeded(result_type seed) {
    auto engine = std::make_shared<std::mt19937_64>(seed);
    return RandomNumberGenerator{[engine] { return (*engine)(); }};
  }
};

struct RandomNumberGeneratorKey
    : DependencyKey<RandomNumberGeneratorKey, RandomNumberGenerator> {
  // A nondeterministic generator seeded from the system entropy source.
  static RandomNumberGenerator liveValue() {
    auto engine = std::make_shared<std::mt19937_64>(std::random_device{}());
    return RandomNumberGenerator{[engine] { return (*engine)(); }};
  }

  // A deterministic generator, so shuffling is reproducible in tests.
  static RandomNumberGenerator testValue() {
    return RandomNumberGenerator::seeded(0);
  }
};

} // namespace Dependencies
