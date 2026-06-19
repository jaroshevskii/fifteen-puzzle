export module ComposableArchitecture:TestStore;

import std;
import :Effect;
import :Reducer;

// A C++ port of TCA's `TestStore`. It drives a reducer the way the real store
// does, but lets a test assert how state changes after each step. Actions
// emitted by effects are not applied automatically; they are queued and must be
// drained with `receive`, mirroring the exhaustive behavior of the Swift library.
export namespace ComposableArchitecture {

template <typename State, typename Action>
class TestStore {
 public:
  template <typename ReducerFactory>
    requires std::invocable<ReducerFactory> &&
        std::convertible_to<std::invoke_result_t<ReducerFactory>, ReducerFunction<State, Action>>
  TestStore(State initialState, ReducerFactory makeReducer)
      : state_(std::move(initialState)), reducer_(makeReducer()) {}

  ~TestStore() {
    if (!pending_.empty()) {
      reportFailure("test store deallocated with " + std::to_string(pending_.size()) +
                    " action(s) left unreceived");
    }
  }

  const State& state() const { return state_; }
  bool failed() const { return failed_; }

  // Sends an action and asserts the resulting state change.
  void send(const Action& action, const std::function<void(State&)>& assert = {}) {
    apply(action, assert, "send");
  }

  // Receives the next effect-produced action and asserts the resulting change.
  void receive(const std::function<void(State&)>& assert = {}) {
    if (pending_.empty()) {
      reportFailure("receive called but no actions were produced by effects");
      return;
    }
    Action action = std::move(pending_.front());
    pending_.pop_front();
    apply(action, assert, "receive");
  }

 private:
  void apply(const Action& action, const std::function<void(State&)>& assert, std::string_view step) {
    State expected = state_;
    Effect<Action> effect = reducer_(state_, action);
    if (effect) {
      Send<Action> capture{[this](Action produced) { pending_.push_back(std::move(produced)); }};
      effect(capture);
    }
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
  ReducerFunction<State, Action> reducer_;
  std::deque<Action> pending_;
  bool failed_ = false;
};

}  // namespace ComposableArchitecture
