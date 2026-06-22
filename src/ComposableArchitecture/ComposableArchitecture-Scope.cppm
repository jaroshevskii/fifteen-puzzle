export module ComposableArchitecture:Scope;

import std;
import :Store;
import :Feature;
import :CasePath;

// A C++ port of TCA 2.0's `Scope(state:action:) { Child() }`. It embeds a child
// feature into a parent domain. The child's `Update`/lifecycle closures receive
// a `ScopedStore` — a lightweight proxy that forwards send/modify/addTask to
// the parent store, translating the child's state slice and action case along
// the way. The proxy is always stack-local; async tasks rebuild it bound to the
// long-lived parent store, so there are no dangling references.
export namespace ComposableArchitecture {

template <typename ParentState, typename ParentAction, typename ChildState,
          typename ChildAction>
class ScopedStore final : public Store<ChildState, ChildAction> {
public:
  ScopedStore(Store<ParentState, ParentAction> &parent,
              ChildState ParentState::*statePath,
              CasePath<ParentAction, ChildAction> actionPath)
      : parent_(parent), statePath_(statePath),
        actionPath_(std::move(actionPath)) {}

  const ChildState &state() const override {
    return parent_.state().*statePath_;
  }
  ChildState snapshot() override { return parent_.snapshot().*statePath_; }

  void send(ChildAction action) override {
    parent_.send(actionPath_.embed(std::move(action)));
  }

  void modify(std::function<void(ChildState &)> mutation) override {
    parent_.modify([statePath = statePath_,
                    mutation = std::move(mutation)](ParentState &parentState) {
      mutation(parentState.*statePath);
    });
  }

  void
  addTask(std::function<void(Store<ChildState, ChildAction> &, std::stop_token)>
              work,
          std::string cancelID = {}) override {
    parent_.addTask(
        [statePath = statePath_, actionPath = actionPath_, parent = &parent_,
         work = std::move(work)](Store<ParentState, ParentAction> &,
                                 std::stop_token token) {
          ScopedStore scoped(*parent, statePath,
                             actionPath); // bound to the long-lived parent
          work(scoped, token);
        },
        std::move(cancelID));
  }

  void cancel(const std::string &cancelID) override {
    parent_.cancel(cancelID);
  }

private:
  Store<ParentState, ParentAction> &parent_;
  ChildState ParentState::*statePath_;
  CasePath<ParentAction, ChildAction> actionPath_;
};

template <typename ParentState, typename ParentAction, typename ChildState,
          typename ChildAction>
Feature<ParentState, ParentAction>
Scope(ChildState ParentState::*statePath,
      CasePath<ParentAction, ChildAction> actionPath,
      Feature<ChildState, ChildAction> child) {
  auto childFeature =
      std::make_shared<Feature<ChildState, ChildAction>>(std::move(child));
  Feature<ParentState, ParentAction> feature;

  feature.addBody([statePath, actionPath,
                   childFeature](ParentState &state, const ParentAction &action,
                                 Store<ParentState, ParentAction> &store) {
    if (auto childAction = actionPath.extract(action)) {
      ScopedStore<ParentState, ParentAction, ChildState, ChildAction> scoped(
          store, statePath, actionPath);
      childFeature->update(state.*statePath, *childAction, scoped);
    }
  });
  feature.onMount([statePath, actionPath,
                   childFeature](ParentState &state,
                                 Store<ParentState, ParentAction> &store) {
    ScopedStore<ParentState, ParentAction, ChildState, ChildAction> scoped(
        store, statePath, actionPath);
    childFeature->mount(state.*statePath, scoped);
  });
  feature.onDismount([statePath, actionPath,
                      childFeature](ParentState &state,
                                    Store<ParentState, ParentAction> &store) {
    ScopedStore<ParentState, ParentAction, ChildState, ChildAction> scoped(
        store, statePath, actionPath);
    childFeature->dismount(state.*statePath, scoped);
  });
  return feature;
}

} // namespace ComposableArchitecture
