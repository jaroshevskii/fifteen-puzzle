export module ComposableArchitecture:Effect;

import std;

// A C++ port of TCA's `Effect<Action>`, including asynchronous, cancellable work.
//
// An effect is a list of items the `Store` runs after a reducer:
//   * sync   — runs inline on the store's thread (`send`, `run`)
//   * async  — runs on a background `std::jthread`, receives a `std::stop_token`
//              for cooperative cancellation (`task`)
//   * cancel — requests cancellation of a running async task by id (`cancel`)
// Build them with the static factories and combine with `merge`.
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

enum class EffectKind { sync, async, cancel };

template <typename Action>
class Effect {
 public:
  template <typename>
  friend class Effect;

  using SyncOp = std::function<void(const Send<Action>&)>;
  using AsyncOp = std::function<void(const Send<Action>&, std::stop_token)>;

  struct Item {
    EffectKind kind = EffectKind::sync;
    SyncOp sync;
    AsyncOp async;
    std::string cancelId;
  };

  // `.none` — does nothing.
  static Effect none() { return Effect{}; }

  // `.send(action)` — feeds another action back into the store.
  static Effect send(Action action) {
    return sync([action = std::move(action)](const Send<Action>& s) { s(action); });
  }

  // `.run { send in ... }` — synchronous work on the store's thread.
  static Effect run(SyncOp op) { return sync(std::move(op)); }

  // `.run { send in await ... }` — asynchronous work on a background thread.
  // The operation receives a `std::stop_token`; honor it to support cancellation.
  static Effect task(AsyncOp op) {
    Effect e;
    e.items_.push_back(Item{.kind = EffectKind::async, .async = std::move(op)});
    return e;
  }

  // `.cancel(id:)` — cancels a running async task started with `.cancellable(id)`.
  static Effect cancel(std::string id) {
    Effect e;
    e.items_.push_back(Item{.kind = EffectKind::cancel, .cancelId = std::move(id)});
    return e;
  }

  // `.merge` — runs several effects.
  static Effect merge(std::vector<Effect> effects) {
    Effect e;
    for (auto& sub : effects) {
      for (auto& item : sub.items_) {
        e.items_.push_back(std::move(item));
      }
    }
    return e;
  }

  // `.cancellable(id:)` — tags this effect's async work so it can be cancelled.
  Effect cancellable(std::string id) && {
    for (auto& item : items_) {
      if (item.kind == EffectKind::async) {
        item.cancelId = id;
      }
    }
    return std::move(*this);
  }

  // Lifts a child effect into a parent's action domain (used by `Scope`).
  template <typename Parent>
  Effect<Parent> map(std::function<Parent(Action)> transform) const {
    Effect<Parent> out;
    for (const auto& item : items_) {
      typename Effect<Parent>::Item parent;
      parent.kind = item.kind;
      parent.cancelId = item.cancelId;
      if (item.kind == EffectKind::sync) {
        parent.sync = [op = item.sync, transform](const Send<Parent>& ps) {
          Send<Action> cs{[&ps, &transform](Action a) { ps(transform(std::move(a))); }};
          op(cs);
        };
      } else if (item.kind == EffectKind::async) {
        parent.async = [op = item.async, transform](const Send<Parent>& ps, std::stop_token st) {
          Send<Action> cs{[&ps, &transform](Action a) { ps(transform(std::move(a))); }};
          op(cs, st);
        };
      }
      out.items_.push_back(std::move(parent));
    }
    return out;
  }

  const std::vector<Item>& items() const { return items_; }
  explicit operator bool() const { return !items_.empty(); }

 private:
  static Effect sync(SyncOp op) {
    Effect e;
    e.items_.push_back(Item{.kind = EffectKind::sync, .sync = std::move(op)});
    return e;
  }

  std::vector<Item> items_;
};

}  // namespace ComposableArchitecture
