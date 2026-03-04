#pragma once

#include <functional>
#include <utility>
#include <vector>

template <typename State, typename Action, typename Effect>
struct Store {
  using Dispatch = std::function<void(const Action&)>;
  using EffectRunner = std::function<void(const std::vector<Effect>&, const Dispatch&)>;
  using Reducer = std::function<std::pair<State, std::vector<Effect>>(const State&, const Action&)>;

  State state{};
  Reducer reducer;
  EffectRunner effectRunner;

  Store(State initialState, Reducer reducer, EffectRunner effectRunner)
      : state(std::move(initialState)),
        reducer(std::move(reducer)),
        effectRunner(std::move(effectRunner)) {}

  void send(const Action& action) {
    auto [nextState, effects] = this->reducer(this->state, action);
    this->state = std::move(nextState);
    this->effectRunner(effects, [this](const Action& nextAction) { this->send(nextAction); });
  }
};
