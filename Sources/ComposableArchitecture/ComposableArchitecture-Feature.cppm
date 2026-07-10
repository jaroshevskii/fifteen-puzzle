export module ComposableArchitecture:Feature;

import std;
import :Store;

// A C++ port of TCA 2.0's `Feature`. A feature is built from `Update` blocks
// (synchronous state mutations) plus lifecycle hooks (`onMount` /
// `onDismount`). `Scope` (see :Scope) produces a Feature that embeds a child
// feature. Compose in a `body()` by building an `Update` and chaining
// `.onMount(...)` etc.
export namespace ComposableArchitecture {

template <typename State, typename Action> class Feature {
public:
  using Body = std::function<void(State &, const Action &, Store<State, Action> &)>;
  using Hook = std::function<void(State &, Store<State, Action> &)>;

  Feature() = default;

  // Lifecycle, chainable: `Update(...).onMount(...).onDismount(...)`.
  Feature &onMount(Hook hook) {
    onMount_.push_back(std::move(hook));
    return *this;
  }
  Feature &onDismount(Hook hook) {
    onDismount_.push_back(std::move(hook));
    return *this;
  }

  // Appends another feature's bodies and hooks (composition).
  Feature &add(Feature other) {
    for (auto &body : other.bodies_)
      bodies_.push_back(std::move(body));
    for (auto &hook : other.onMount_)
      onMount_.push_back(std::move(hook));
    for (auto &hook : other.onDismount_)
      onDismount_.push_back(std::move(hook));
    return *this;
  }

  void addBody(Body body) { bodies_.push_back(std::move(body)); }

  // A C++ port of TCA 2.0's `.onChange(of:)` trigger. `project(state)` derives
  // a value; whenever it differs from the value seen after the previous update,
  // `handler(oldValue, newValue, state, store)` runs. The baseline is seeded
  // from the post-`onMount` state, so a change caused by the very first action
  // still fires (matching TCA, where the initial state is the baseline);
  // `onMount`'s own effect on the value never fires. Implemented as a body that
  // runs in append order, so chain it AFTER the `Update`/`Scope` bodies whose
  // effect you want to observe. The per-feature baseline lives in a
  // `shared_ptr` captured by the closures, so it persists across updates (the
  // feature outlives individual actions).
  template <typename Project, typename Handler>
  Feature &onChange(Project project, Handler handler) {
    using Value = std::decay_t<std::invoke_result_t<Project, const State &>>;
    auto previous = std::make_shared<std::optional<Value>>();
    onMount_.push_back([project, previous](State &state, Store<State, Action> &) {
      *previous = project(std::as_const(state));
    });
    bodies_.push_back([project = std::move(project), handler = std::move(handler),
                       previous](State &state, const Action &, Store<State, Action> &store) {
      Value current = project(std::as_const(state));
      if (previous->has_value() && !(**previous == current)) {
        handler(**previous, current, state, store);
      }
      *previous = std::move(current);
    });
    return *this;
  }

  // Invoked by the runtime / scope.
  void update(State &state, const Action &action, Store<State, Action> &store) const {
    for (const auto &body : bodies_)
      body(state, action, store);
  }
  void mount(State &state, Store<State, Action> &store) const {
    for (const auto &hook : onMount_)
      hook(state, store);
  }
  void dismount(State &state, Store<State, Action> &store) const {
    for (const auto &hook : onDismount_)
      hook(state, store);
  }

private:
  std::vector<Body> bodies_;
  std::vector<Hook> onMount_;
  std::vector<Hook> onDismount_;
};

// `Update { state, action in ... }` — a synchronous state mutation. The closure
// also receives the store for enqueuing async work.
template <typename State, typename Action>
Feature<State, Action> Update(typename Feature<State, Action>::Body body) {
  Feature<State, Action> feature;
  feature.addBody(std::move(body));
  return feature;
}

} // namespace ComposableArchitecture
