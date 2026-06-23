export module ComposableArchitecture:Runtime;

import std;
import :Store;
import :Feature;

// A C++ port of TCA 2.0's root `Store`. It owns the single source-of-truth
// state and runs the feature. State is mutated only on the store's main thread:
// `send`/`modify` from the main thread apply immediately; called from a
// background task they enqueue, and the main loop applies them in `pump()`.
// Async work runs on `std::jthread`s and is cooperatively cancellable.
export namespace ComposableArchitecture {

template <typename State, typename Action> class RootStore final : public Store<State, Action> {
public:
  template <typename FeatureFactory>
    requires std::invocable<FeatureFactory> &&
                 std::same_as<std::invoke_result_t<FeatureFactory>, Feature<State, Action>>
  RootStore(State initial, FeatureFactory makeFeature)
      : state_(std::move(initial)), feature_(makeFeature()),
        mainThread_(std::this_thread::get_id()) {
    std::lock_guard lock(stateMutex_);
    feature_.mount(state_,
                   *this); // onMount — like TCA 2.0, runs once at creation
  }

  ~RootStore() override {
    {
      std::lock_guard lock(stateMutex_);
      feature_.dismount(state_, *this);
    }
    std::lock_guard lock(tasksMutex_);
    cancellableTasks_.clear(); // jthreads request_stop + join
    anonymousTasks_.clear();
  }

  const State &state() const override { return state_; }
  State snapshot() override {
    std::lock_guard lock(stateMutex_);
    return state_;
  }

  void send(Action action) override {
    {
      std::lock_guard lock(queueMutex_);
      inbox_.push_back(Work{.action = std::move(action)});
    }
    drainIfMain();
  }

  void modify(std::function<void(State &)> mutation) override {
    {
      std::lock_guard lock(queueMutex_);
      inbox_.push_back(Work{.mutation = std::move(mutation)});
    }
    drainIfMain();
  }

  void addTask(std::function<void(Store<State, Action> &, std::stop_token)> work,
               std::string cancelID = {}) override {
    std::lock_guard lock(tasksMutex_);
    if (cancelID.empty()) {
      // Fire-and-forget: reap completed tasks so they don't accumulate.
      std::erase_if(anonymousTasks_, [](const AnonymousTask &task) { return task.done->load(); });
      auto done = std::make_shared<std::atomic<bool>>(false);
      std::jthread worker([this, work = std::move(work), done](std::stop_token token) {
        work(*this, token);
        done->store(true);
      });
      anonymousTasks_.push_back(
          AnonymousTask{.done = std::move(done), .thread = std::move(worker)});
    } else {
      // Replacing an in-flight task with the same id requests its stop and
      // joins.
      cancellableTasks_[cancelID] = std::jthread(
          [this, work = std::move(work)](std::stop_token token) { work(*this, token); });
    }
  }

  void cancel(const std::string &cancelID) override {
    std::lock_guard lock(tasksMutex_);
    if (auto it = cancellableTasks_.find(cancelID); it != cancellableTasks_.end()) {
      it->second.request_stop();
      cancellableTasks_.erase(it);
    }
  }

  // Process actions/mutations queued by background tasks. Call once per frame.
  void pump() { drainIfMain(); }

private:
  struct Work {
    std::optional<Action> action;
    std::function<void(State &)> mutation;
  };

  void drainIfMain() {
    if (std::this_thread::get_id() != mainThread_) {
      return; // background tasks only enqueue; the main thread applies
    }
    if (draining_) {
      return; // re-entrancy guard
    }
    draining_ = true;
    while (true) {
      Work work;
      {
        std::lock_guard lock(queueMutex_);
        if (inbox_.empty()) {
          break;
        }
        work = std::move(inbox_.front());
        inbox_.pop_front();
      }
      std::lock_guard lock(stateMutex_);
      if (work.action) {
        feature_.update(state_, *work.action, *this);
      } else if (work.mutation) {
        work.mutation(state_);
      }
    }
    draining_ = false;
  }

  State state_;
  Feature<State, Action> feature_;
  std::thread::id mainThread_;

  std::mutex stateMutex_;
  std::mutex queueMutex_;
  std::mutex tasksMutex_;
  std::deque<Work> inbox_;
  bool draining_ = false;

  struct AnonymousTask {
    std::shared_ptr<std::atomic<bool>> done;
    std::jthread thread;
  };

  std::unordered_map<std::string, std::jthread> cancellableTasks_;
  std::vector<AnonymousTask> anonymousTasks_;
};

} // namespace ComposableArchitecture
