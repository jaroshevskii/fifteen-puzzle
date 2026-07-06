export module ComposableArchitecture:TestStore;

import std;
import :Store;
import :Feature;

// A C++ port of TCA 2.0's `TestStore`. It runs the feature like the real store
// but deterministically: `onMount` runs at construction, async tasks run inline
// (no threads), `modify` applies immediately, and actions sent from tasks are
// queued for `receive`. Each `send`/`receive` asserts the resulting state.
export namespace ComposableArchitecture {

template <typename State, typename Action> class TestStore final : public Store<State, Action> {
public:
  template <typename FeatureFactory>
    requires std::invocable<FeatureFactory> &&
                 std::same_as<std::invoke_result_t<FeatureFactory>, Feature<State, Action>>
  TestStore(State initial, FeatureFactory makeFeature)
      : state_(std::move(initial)), feature_(makeFeature()) {
    feature_.mount(state_, *this); // onMount runs once, as in the live store
  }

  ~TestStore() override {
    if (!pending_.empty()) {
      reportFailure("test store deallocated with " + std::to_string(pending_.size()) +
                    " action(s) left unreceived");
    }
  }

  // --- Store interface (used by the feature under test) ---------------------
  const State &state() const override { return state_; }
  State snapshot() override { return state_; }
  void send(Action action) override { pending_.push_back(std::move(action)); }
  void modify(std::function<void(State &)> mutation) override { mutation(state_); }
  void addTask(std::function<void(Store<State, Action> &, std::stop_token)> work,
               std::string = {}) override {
    std::stop_source source;
    work(*this, source.get_token()); // inline, deterministic
  }
  void cancel(const std::string &) override {}

  // --- test driver ----------------------------------------------------------
  // The assert closure is required (pass `{}` for "no change expected"); this
  // keeps the two-argument driver distinct from the one-argument Store::send
  // that features call from tasks.
  void send(const Action &action, const std::function<void(State &)> &assert) {
    apply(action, assert, "send");
  }
  void receive(const std::function<void(State &)> &assert = {}) {
    if (pending_.empty()) {
      reportFailure("receive called but no actions were produced");
      return;
    }
    Action action = std::move(pending_.front());
    pending_.pop_front();
    apply(action, assert, "receive");
  }

  bool failed() const { return failed_; }

private:
  void apply(const Action &action, const std::function<void(State &)> &assert,
             std::string_view step) {
    State expected = state_;
    feature_.update(state_, action, *this);
    if (assert) {
      assert(expected);
    }
    if (!(expected == state_)) {
      reportFailure(std::string("state did not match expectation after ") + std::string(step));
    }
  }

  void reportFailure(std::string_view message) {
    failed_ = true;
    std::println(std::cerr, "TestStore failure: {}", message);
  }

  State state_;
  Feature<State, Action> feature_;
  std::deque<Action> pending_;
  bool failed_ = false;
};

} // namespace ComposableArchitecture
