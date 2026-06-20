export module ComposableArchitecture:Store;

import std;
import :Effect;
import :Reducer;

// A C++ port of TCA's `Store`, with support for asynchronous effects.
//
// State is mutated only on the thread that drives the store (the main loop):
// `send` enqueues an action and processes the queue there. Async effects run on
// background `std::jthread`s and feed actions back through a thread-safe `Send`,
// which simply enqueues; the main loop picks them up via `pump()`. So reducers
// stay single-threaded and race-free even though real work happens off-thread.
export namespace ComposableArchitecture {

template <typename State, typename Action>
class Store {
 public:
  template <typename ReducerFactory>
    requires std::invocable<ReducerFactory> &&
        std::convertible_to<std::invoke_result_t<ReducerFactory>, ReducerFunction<State, Action>>
  Store(State initialState, ReducerFactory makeReducer)
      : state_(std::move(initialState)), reducer_(makeReducer()) {}

  const State& state() const { return state_; }

  // Sends an action and processes it (and any synchronous follow-ups) inline.
  void send(const Action& action) {
    enqueue(action);
    drain();
  }

  // Processes actions queued by background tasks. Call once per frame.
  void pump() { drain(); }

 private:
  using Item = typename Effect<Action>::Item;

  void enqueue(const Action& action) {
    std::lock_guard lock(mutex_);
    inbox_.push_back(action);
  }

  bool dequeue(Action& out) {
    std::lock_guard lock(mutex_);
    if (inbox_.empty()) {
      return false;
    }
    out = std::move(inbox_.front());
    inbox_.pop_front();
    return true;
  }

  void drain() {
    if (draining_) {
      return;  // reentrancy guard; only the main thread drains
    }
    draining_ = true;
    Action action;
    while (dequeue(action)) {
      runEffect(reducer_(state_, action));
    }
    draining_ = false;
  }

  void runEffect(const Effect<Action>& effect) {
    Send<Action> send{[this](Action next) { this->enqueue(next); }};
    for (const auto& item : effect.items()) {
      switch (item.kind) {
        case EffectKind::sync:
          item.sync(send);
          break;
        case EffectKind::async: {
          std::jthread worker([op = item.async, send](std::stop_token token) { op(send, token); });
          if (item.cancelId.empty()) {
            anonymousTasks_.push_back(std::move(worker));
          } else {
            // Move-assigning over a running jthread requests its stop and joins,
            // so starting a task with an in-use id cancels the previous one.
            cancellableTasks_[item.cancelId] = std::move(worker);
          }
          break;
        }
        case EffectKind::cancel: {
          if (auto it = cancellableTasks_.find(item.cancelId); it != cancellableTasks_.end()) {
            it->second.request_stop();
            cancellableTasks_.erase(it);
          }
          break;
        }
      }
    }
  }

  State state_;
  ReducerFunction<State, Action> reducer_;

  std::mutex mutex_;
  std::deque<Action> inbox_;
  bool draining_ = false;

  std::unordered_map<std::string, std::jthread> cancellableTasks_;
  std::vector<std::jthread> anonymousTasks_;
};

}  // namespace ComposableArchitecture
