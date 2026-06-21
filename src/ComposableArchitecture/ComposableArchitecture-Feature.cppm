export module ComposableArchitecture:Feature;

import std;
import :Store;

// A C++ port of TCA 2.0's `Feature`. A feature is built from `Update` blocks
// (synchronous state mutations) plus lifecycle hooks (`onMount` / `onDismount`).
// `Scope` (see :Scope) produces a Feature that embeds a child feature. Compose
// in a `body()` by building an `Update` and chaining `.onMount(...)` etc.
export namespace ComposableArchitecture {

template <typename State, typename Action>
class Feature {
 public:
  using Body = std::function<void(State&, const Action&, Store<State, Action>&)>;
  using Hook = std::function<void(State&, Store<State, Action>&)>;

  Feature() = default;

  // Lifecycle, chainable: `Update(...).onMount(...).onDismount(...)`.
  Feature& onMount(Hook hook) {
    onMount_.push_back(std::move(hook));
    return *this;
  }
  Feature& onDismount(Hook hook) {
    onDismount_.push_back(std::move(hook));
    return *this;
  }

  // Appends another feature's bodies and hooks (composition).
  Feature& add(Feature other) {
    for (auto& body : other.bodies_) bodies_.push_back(std::move(body));
    for (auto& hook : other.onMount_) onMount_.push_back(std::move(hook));
    for (auto& hook : other.onDismount_) onDismount_.push_back(std::move(hook));
    return *this;
  }

  void addBody(Body body) { bodies_.push_back(std::move(body)); }

  // Invoked by the runtime / scope.
  void update(State& state, const Action& action, Store<State, Action>& store) const {
    for (const auto& body : bodies_) body(state, action, store);
  }
  void mount(State& state, Store<State, Action>& store) const {
    for (const auto& hook : onMount_) hook(state, store);
  }
  void dismount(State& state, Store<State, Action>& store) const {
    for (const auto& hook : onDismount_) hook(state, store);
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

}  // namespace ComposableArchitecture
