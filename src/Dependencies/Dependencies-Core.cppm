export module Dependencies:Core;

import std;

// A C++ port of Point-Free's `Dependencies` library.
//
// `DependencyValues` is a type-erased dictionary keyed by `DependencyKey` types
// (mirroring the Swift library's heterogeneous storage). A dependency is read
// through the `Dependency<Key>` accessor, which always resolves against the
// current `DependencyValues` for the running scope. Scopes are introduced with
// `withDependencies` (override for a closure) and `prepareDependencies` (set
// the global defaults at app launch).
export namespace Dependencies {

// The execution context, mirroring TCA's notion of live / preview / test.
enum class DependencyContext : std::uint8_t {
  live,
  preview,
  test,
};

// CRTP base for declaring a dependency key. `previewValue` and `testValue`
// default to `liveValue`, but either can be overridden by the concrete key.
template <typename Self, typename ValueType> struct DependencyKey {
  using Value = ValueType;

  static Value previewValue() { return Self::liveValue(); }
  static Value testValue() { return Self::liveValue(); }
};

class DependencyValues {
public:
  DependencyContext context = DependencyContext::live;

  DependencyValues() = default;
  DependencyValues(const DependencyValues &other) { copyFrom(other); }
  DependencyValues &operator=(const DependencyValues &other) {
    if (this != &other) {
      copyFrom(other);
    }
    return *this;
  }
  DependencyValues(DependencyValues &&) = default;
  DependencyValues &operator=(DependencyValues &&) = default;

  // Reads the value for a key, lazily resolving the context-appropriate default
  // the first time it is requested. Equivalent to `self[Key.self]`.
  template <typename Key> typename Key::Value &get() {
    const auto index = std::type_index(typeid(Key));
    auto it = storage_.find(index);
    if (it == storage_.end()) {
      it = storage_
               .emplace(index, std::make_shared<Model<typename Key::Value>>(
                                   resolveDefault<Key>()))
               .first;
    }
    return static_cast<Model<typename Key::Value> *>(it->second.get())->value;
  }

  // Overrides the value for a key. Equivalent to `self[Key.self] = newValue`.
  template <typename Key> void set(typename Key::Value value) {
    storage_[std::type_index(typeid(Key))] =
        std::make_shared<Model<typename Key::Value>>(std::move(value));
  }

  // The global root values, used as the base for every scope.
  static DependencyValues &root() {
    static DependencyValues values;
    return values;
  }

  // The values for the currently running scope.
  static DependencyValues &current() { return **currentSlot(); }

  static DependencyValues **currentSlot() {
    static thread_local DependencyValues *current = &root();
    return &current;
  }

private:
  struct Concept {
    virtual ~Concept() = default;
    virtual std::shared_ptr<Concept> clone() const = 0;
  };

  template <typename T> struct Model : Concept {
    T value;
    explicit Model(T value) : value(std::move(value)) {}
    std::shared_ptr<Concept> clone() const override {
      return std::make_shared<Model<T>>(value);
    }
  };

  void copyFrom(const DependencyValues &other) {
    context = other.context;
    storage_.clear();
    for (const auto &[index, model] : other.storage_) {
      storage_.emplace(index, model->clone());
    }
  }

  template <typename Key> typename Key::Value resolveDefault() {
    switch (context) {
    case DependencyContext::live:
      return Key::liveValue();
    case DependencyContext::preview:
      return Key::previewValue();
    case DependencyContext::test:
      return Key::testValue();
    }
    return Key::liveValue();
  }

  std::unordered_map<std::type_index, std::shared_ptr<Concept>> storage_;
};

// The C++ analog of the `@Dependency` property wrapper. It holds no state and
// resolves against `DependencyValues::current()` on every access, so a reducer
// can declare its dependencies locally and still observe overrides made by an
// enclosing `withDependencies` or `prepareDependencies`.
template <typename Key> class Dependency {
public:
  typename Key::Value &value() const {
    return DependencyValues::current().template get<Key>();
  }
  typename Key::Value &operator*() const { return value(); }
  typename Key::Value *operator->() const { return &value(); }
};

// Overrides dependencies for the duration of `operation`, restoring the
// previous scope afterwards (even if `operation` throws).
template <typename Operation>
auto withDependencies(const std::function<void(DependencyValues &)> &update,
                      Operation operation) -> decltype(operation()) {
  DependencyValues scoped = DependencyValues::current();
  update(scoped);

  DependencyValues **slot = DependencyValues::currentSlot();
  DependencyValues *previous = *slot;
  *slot = &scoped;

  struct Restore {
    DependencyValues **slot;
    DependencyValues *previous;
    ~Restore() { *slot = previous; }
  } restore{slot, previous};

  return operation();
}

// Sets dependency values globally, intended to be called as early as possible
// at app launch. Equivalent to TCA's `prepareDependencies`.
inline void
prepareDependencies(const std::function<void(DependencyValues &)> &update) {
  update(DependencyValues::root());
}

} // namespace Dependencies
