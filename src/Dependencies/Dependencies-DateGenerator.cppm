export module Dependencies:DateGenerator;

import std;
import :Core;

// A controllable clock, mirroring TCA's `\.date` dependency. Reading the
// current time goes through this dependency instead of calling a global clock
// directly, which lets tests and previews pin time to a fixed value.
export namespace Dependencies {

struct DateGenerator {
  std::function<double()> generate;

  // The current time, in seconds. Equivalent to `date.now`.
  double now() const { return generate(); }

  // A generator that always reports the same instant, for tests and previews.
  static DateGenerator constant(double seconds) {
    return DateGenerator{[seconds] { return seconds; }};
  }
};

struct DateGeneratorKey : DependencyKey<DateGeneratorKey, DateGenerator> {
  // A monotonic wall clock, measured in seconds from first use.
  static DateGenerator liveValue() {
    const auto start = std::chrono::steady_clock::now();
    return DateGenerator{[start] {
      return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                           start)
          .count();
    }};
  }

  // Tests start pinned at zero and opt into specific instants via overrides.
  static DateGenerator testValue() { return DateGenerator::constant(0.0); }
};

} // namespace Dependencies
