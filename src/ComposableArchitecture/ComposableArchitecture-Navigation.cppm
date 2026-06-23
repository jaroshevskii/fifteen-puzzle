export module ComposableArchitecture:Navigation;

import std;
import :Store;
import :Feature;
import :CasePath;

// A C++ port of TCA 2.0's state-driven navigation — the analog of
// `ifLet(\.$destination, action: \.destination) { Destination.body }`. A parent
// holds an `std::optional<Destination>` where `Destination` is a `std::variant`
// of screen states; `ifCaseLet` embeds a child feature into one case of that
// variant, running it only while that case is presented. This is how exactly
// one screen is active at a time, with the rest of the parent state preserved.
export namespace ComposableArchitecture {

// A case path for selecting one alternative of a `std::variant` whose
// alternative *is* the child state directly (no wrapping member) — the state
// analog of `casePath` for action variants.
template <typename Variant, typename Alternative> CasePath<Variant, Alternative> caseState() {
  return CasePath<Variant, Alternative>{
      [](const Variant &whole) -> std::optional<Alternative> {
        if (const auto *value = std::get_if<Alternative>(&whole)) {
          return *value;
        }
        return std::nullopt;
      },
      [](Alternative value) -> Variant { return Variant{std::move(value)}; }};
}

// The child's view of the store while presented inside `optional<Destination>`.
// `modify`/`snapshot` re-check the active case every time, so an action that
// arrives after the destination was dismissed or switched (e.g. a late
// background response) becomes a no-op instead of touching stale state — the
// same guarantee TCA's `ifLet` provides.
template <typename ParentState, typename ParentAction, typename Destination, typename ChildState,
          typename ChildAction>
class PresentationScopedStore final : public Store<ChildState, ChildAction> {
public:
  PresentationScopedStore(Store<ParentState, ParentAction> &parent,
                          std::optional<Destination> ParentState::*destinationPath,
                          CasePath<Destination, ChildState> stateCase,
                          CasePath<ParentAction, ChildAction> actionPath)
      : parent_(parent), destinationPath_(destinationPath), stateCase_(std::move(stateCase)),
        actionPath_(std::move(actionPath)) {}

  const ChildState &state() const override {
    // Only valid while the case is active; the synchronous `ifCaseLet` body
    // never calls this (it passes the live reference straight to the child).
    const auto &destination = parent_.state().*destinationPath_;
    return *std::get_if<ChildState>(&*destination);
  }

  ChildState snapshot() override {
    const ParentState parent = parent_.snapshot();
    const auto &destination = parent.*destinationPath_;
    if (destination) {
      if (const auto extracted = stateCase_.extract(*destination)) {
        return *extracted;
      }
    }
    return ChildState{};
  }

  void send(ChildAction action) override { parent_.send(actionPath_.embed(std::move(action))); }

  void modify(std::function<void(ChildState &)> mutation) override {
    parent_.modify([destinationPath = destinationPath_,
                    mutation = std::move(mutation)](ParentState &parentState) {
      auto &destination = parentState.*destinationPath;
      if (!destination) {
        return; // dismissed before this ran
      }
      if (auto *child = std::get_if<ChildState>(&*destination)) {
        mutation(*child); // still the same case — safe to mutate in place
      }
    });
  }

  void addTask(std::function<void(Store<ChildState, ChildAction> &, std::stop_token)> work,
               std::string cancelID = {}) override {
    parent_.addTask(
        [destinationPath = destinationPath_, stateCase = stateCase_, actionPath = actionPath_,
         parent = &parent_,
         work = std::move(work)](Store<ParentState, ParentAction> &, std::stop_token token) {
          PresentationScopedStore scoped(*parent, destinationPath, stateCase, actionPath);
          work(scoped, token);
        },
        std::move(cancelID));
  }

  void cancel(const std::string &cancelID) override { parent_.cancel(cancelID); }

private:
  Store<ParentState, ParentAction> &parent_;
  std::optional<Destination> ParentState::*destinationPath_;
  CasePath<Destination, ChildState> stateCase_;
  CasePath<ParentAction, ChildAction> actionPath_;
};

// Runs `child` against the case `stateCase` of the presented `destinationPath`,
// for actions matched by `actionPath` — and only while that case is presented.
// Mirrors `Scope`'s body, guarding on both the action case and the active state
// case. Lifecycle (onMount on present / onDismount on dismiss) is intentionally
// left to the parent: present the destination, then dispatch an "appeared"
// action — simpler and free of mid-reducer ordering hazards.
template <typename ParentState, typename ParentAction, typename Destination, typename ChildState,
          typename ChildAction>
Feature<ParentState, ParentAction>
ifCaseLet(std::optional<Destination> ParentState::*destinationPath,
          CasePath<Destination, ChildState> stateCase,
          CasePath<ParentAction, ChildAction> actionPath, Feature<ChildState, ChildAction> child) {
  auto childFeature = std::make_shared<Feature<ChildState, ChildAction>>(std::move(child));
  Feature<ParentState, ParentAction> feature;

  feature.addBody([destinationPath, stateCase, actionPath,
                   childFeature](ParentState &state, const ParentAction &action,
                                 Store<ParentState, ParentAction> &store) {
    const auto childAction = actionPath.extract(action);
    if (!childAction) {
      return; // action is not addressed to this child
    }
    auto &destination = state.*destinationPath;
    if (!destination) {
      return; // nothing presented
    }
    if (auto *childState = std::get_if<ChildState>(&*destination)) {
      PresentationScopedStore<ParentState, ParentAction, Destination, ChildState, ChildAction>
          scoped(store, destinationPath, stateCase, actionPath);
      childFeature->update(*childState, *childAction, scoped);
    }
  });

  return feature;
}

} // namespace ComposableArchitecture
