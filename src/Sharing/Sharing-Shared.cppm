export module Sharing:Shared;

import std;
import :Strategy;

// A C++ port of Point-Free's `@Shared` — minus the property-wrapper magic. A
// `Shared<T>` is a plain value (so it lives in feature State and participates
// in `operator==`) coupled to a `PersistenceStrategy<T>`. It loads from the
// strategy on construction; mutations update the in-memory value synchronously.
//
// Crucially, mutating does NOT perform I/O: persistence is a side-effect the
// feature dispatches through the store's `addTask` (capturing `strategy()` and
// the new value). That keeps `Shared` free of any store/threading knowledge and
// keeps saves off the main thread, matching the rest of the architecture.
export namespace Sharing {

template <typename T> class Shared {
public:
  Shared() = default;

  explicit Shared(PersistenceStrategy<T> strategy)
      : strategy_(std::move(strategy)) {
    if (auto loaded = strategy_.load()) {
      value_ = std::move(*loaded);
    }
  }

  const T &get() const { return value_; }

  void set(T value) { value_ = std::move(value); }

  // Mutates the value in place and returns it, so the caller can immediately
  // dispatch the persisted write: `store.addTask([s = shared.strategy(),
  // v = shared.withMutation(f)](...) { s.save(v); });`
  template <typename F> const T &withMutation(F &&f) {
    std::forward<F>(f)(value_);
    return value_;
  }

  const PersistenceStrategy<T> &strategy() const { return strategy_; }

  // Equality compares only the value — never the `std::function` strategy
  // (which isn't comparable). This is what lets a State holding a `Shared`
  // stay `= default`-comparable for the TestStore's state diffing.
  bool operator==(const Shared &rhs) const { return value_ == rhs.value_; }

private:
  T value_{};
  PersistenceStrategy<T> strategy_{};
};

} // namespace Sharing
