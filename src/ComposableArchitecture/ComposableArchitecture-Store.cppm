export module ComposableArchitecture:Store;

import std;
import :Effect;
import :Reducer;

// A C++ port of TCA's `Store`. The store owns a single source-of-truth `State`,
// and `send` feeds an action through the root reducer, applies the resulting
// state mutation, and then executes the returned effect. Effects run
// synchronously and may send further actions, which re-enter `send`.
export namespace ComposableArchitecture {

template <typename State, typename Action>
class Store {
 public:
  // Builds a store from an initial state and a reducer factory, mirroring
  // `Store(initialState:) { Feature() }`.
  template <typename ReducerFactory>
    requires std::invocable<ReducerFactory> &&
        std::convertible_to<std::invoke_result_t<ReducerFactory>, ReducerFunction<State, Action>>
  Store(State initialState, ReducerFactory makeReducer)
      : state_(std::move(initialState)), reducer_(makeReducer()) {}

  const State& state() const { return state_; }

  // Sends an action through the reducer and runs any resulting effect.
  void send(const Action& action) {
    Effect<Action> effect = reducer_(state_, action);
    if (effect) {
      Send<Action> send{[this](Action nextAction) { this->send(nextAction); }};
      effect(send);
    }
  }

 private:
  State state_;
  ReducerFunction<State, Action> reducer_;
};

}  // namespace ComposableArchitecture
