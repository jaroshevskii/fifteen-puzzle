export module ComposableArchitecture:Store;

import std;

// The store interface a feature interacts with — the C++ analog of TCA 2.0's
// implicitly-available `store`. Synchronous state changes happen in `Update`;
// asynchronous work is enqueued with `addTask` and can read/modify state and
// send actions back. The concrete root store (`RootStore`) and the scoped child
// proxy (`ScopedStore`) both implement this interface.
export namespace ComposableArchitecture {

template <typename State, typename Action> class Store {
public:
  virtual ~Store() = default;

  // Current state (read on the store's own thread).
  virtual const State &state() const = 0;
  // A thread-safe copy, for reading state from inside an async task.
  virtual State snapshot() = 0;

  // Feed an action back through the feature.
  virtual void send(Action action) = 0;
  // Mutate state directly (serialized on the store's thread).
  virtual void modify(std::function<void(State &)> mutation) = 0;

  // Enqueue async work on a background thread. The closure receives this store
  // (to send/modify/read) and a stop token for cooperative cancellation. A
  // non-empty `cancelID` lets the task be cancelled later with `cancel`.
  virtual void addTask(std::function<void(Store &, std::stop_token)> work,
                       std::string cancelID = {}) = 0;
  // Request cancellation of a task started with the given id.
  virtual void cancel(const std::string &cancelID) = 0;
};

} // namespace ComposableArchitecture
