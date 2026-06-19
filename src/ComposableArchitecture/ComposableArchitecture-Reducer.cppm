export module ComposableArchitecture:Reducer;

import std;
import :CasePath;
import :Effect;

// A C++ port of TCA's reducer building blocks. A reducer is modeled as a
// `ReducerFunction<State, Action>`: a function that mutates state in place and
// returns an `Effect<Action>` describing any follow-up work. Features expose
// their logic through a `body()` returning such a function, built up from
// `Reduce`, `Scope`, and `combine` (the analog of `CombineReducers`).
export namespace ComposableArchitecture {

template <typename State, typename Action>
using ReducerFunction = std::function<Effect<Action>(State&, const Action&)>;

// Constrains a callable to the shape of a reducer over `State`/`Action`.
template <typename R, typename State, typename Action>
concept Reducer = std::invocable<R, State&, const Action&> &&
    std::same_as<std::invoke_result_t<R, State&, const Action&>, Effect<Action>>;

// Wraps a closure as a reducer. Equivalent to `Reduce { state, action in ... }`.
template <typename State, typename Action>
ReducerFunction<State, Action> Reduce(ReducerFunction<State, Action> reduce) {
  return reduce;
}

// Embeds a child reducer into a parent domain along a state member pointer and
// an action case path. Equivalent to `Scope(state: \.child, action: \.child)`.
template <typename ParentState, typename ParentAction, typename ChildState, typename ChildAction>
ReducerFunction<ParentState, ParentAction> Scope(
    ChildState ParentState::* statePath,
    CasePath<ParentAction, ChildAction> actionPath,
    ReducerFunction<ChildState, ChildAction> child) {
  return [statePath, actionPath = std::move(actionPath), child = std::move(child)](
             ParentState& state, const ParentAction& action) -> Effect<ParentAction> {
    std::optional<ChildAction> childAction = actionPath.extract(action);
    if (!childAction) {
      return Effect<ParentAction>::none();
    }
    Effect<ChildAction> childEffect = child(state.*statePath, *childAction);
    return childEffect.template map<ParentAction>(actionPath.embed);
  };
}

// Runs several reducers against the same domain in order, merging their effects.
// Equivalent to `CombineReducers`.
template <typename State, typename Action>
ReducerFunction<State, Action> combine(std::vector<ReducerFunction<State, Action>> reducers) {
  return [reducers = std::move(reducers)](State& state, const Action& action) -> Effect<Action> {
    std::vector<Effect<Action>> effects;
    for (const auto& reducer : reducers) {
      Effect<Action> effect = reducer(state, action);
      if (effect) {
        effects.push_back(std::move(effect));
      }
    }
    return Effect<Action>::merge(std::move(effects));
  };
}

}  // namespace ComposableArchitecture
