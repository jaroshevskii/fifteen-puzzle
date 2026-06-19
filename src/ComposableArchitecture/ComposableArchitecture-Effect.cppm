export module ComposableArchitecture:Effect;

import std;

// A C++ port of TCA's `Effect<Action>`. An effect describes work to be executed
// by the `Store` after a reducer runs, producing zero or more follow-up actions
// through a `Send`. Effects are values: build them with the static factories
// `none`, `send`, `run`, and `merge`.
export namespace ComposableArchitecture {

template <typename Action>
class Send {
 public:
  using Function = std::function<void(Action)>;

  explicit Send(Function function) : function_(std::move(function)) {}

  void operator()(Action action) const { function_(std::move(action)); }

 private:
  Function function_;
};

template <typename Action>
class Effect {
 public:
  using Operation = std::function<void(const Send<Action>&)>;

  // An effect that does nothing. Equivalent to `.none`.
  static Effect none() { return Effect{}; }

  // An effect that immediately feeds another action back into the store.
  static Effect send(Action action) {
    return Effect{[action = std::move(action)](const Send<Action>& send) { send(action); }};
  }

  // An effect that performs work, optionally sending actions back into the
  // store. Equivalent to `.run { send in ... }`.
  static Effect run(Operation operation) { return Effect{std::move(operation)}; }

  // Combines several effects, running them in order. Equivalent to `.merge`.
  static Effect merge(std::vector<Effect> effects) {
    if (effects.empty()) {
      return none();
    }
    return Effect{[effects = std::move(effects)](const Send<Action>& send) {
      for (const auto& effect : effects) {
        effect(send);
      }
    }};
  }

  // Transforms the actions produced by this effect, lifting a child effect into
  // a parent's action domain. Used by `Scope` to embed child effects.
  template <typename ParentAction>
  Effect<ParentAction> map(std::function<ParentAction(Action)> transform) const {
    if (!operation_) {
      return Effect<ParentAction>::none();
    }
    return Effect<ParentAction>::run(
        [operation = operation_, transform = std::move(transform)](const Send<ParentAction>& parentSend) {
          Send<Action> childSend{[&parentSend, &transform](Action childAction) {
            parentSend(transform(std::move(childAction)));
          }};
          operation(childSend);
        });
  }

  void operator()(const Send<Action>& send) const {
    if (operation_) {
      operation_(send);
    }
  }

  // True when the effect actually performs work (i.e. it is not `.none`).
  explicit operator bool() const { return static_cast<bool>(operation_); }

 private:
  Effect() = default;
  explicit Effect(Operation operation) : operation_(std::move(operation)) {}

  Operation operation_;
};

}  // namespace ComposableArchitecture
