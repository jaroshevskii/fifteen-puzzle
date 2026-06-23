// Tests the ifCaseLet navigation primitive on a toy parent/child domain: a
// child feature embedded in one case of a presented optional<variant>. Verifies
// the child runs only while its case is presented (the late/wrong-case action
// guard), and that caseState round-trips.

import std;
import ComposableArchitecture;

using ComposableArchitecture::casePath;
using ComposableArchitecture::caseState;
using ComposableArchitecture::Feature;
using ComposableArchitecture::ifCaseLet;
using ComposableArchitecture::Store;
using ComposableArchitecture::TestStore;
using ComposableArchitecture::Update;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

// --- Child domain ---
struct Counter {
  int value = 0;
  bool operator==(const Counter &) const = default;
};
struct Increment {};
using CounterAction = std::variant<Increment>;

Feature<Counter, CounterAction> counterBody() {
  return Update<Counter, CounterAction>([](Counter &state, const CounterAction &,
                                           Store<Counter, CounterAction> &) { ++state.value; });
}

// --- Parent domain ---
struct OtherScreen {
  bool operator==(const OtherScreen &) const = default;
};
using Destination = std::variant<Counter, OtherScreen>;

struct Child {
  CounterAction action;
};
struct PresentCounter {};
struct PresentOther {};
struct Dismiss {};
using ParentAction = std::variant<Child, PresentCounter, PresentOther, Dismiss>;

struct ParentState {
  std::optional<Destination> destination;
  bool operator==(const ParentState &) const = default;
};

Feature<ParentState, ParentAction> parentBody() {
  using ParentStore = Store<ParentState, ParentAction>;
  auto feature = Update<ParentState, ParentAction>(
      [](ParentState &state, const ParentAction &action, ParentStore &) {
        std::visit(
            [&](auto &&value) {
              using V = std::decay_t<decltype(value)>;
              if constexpr (std::is_same_v<V, PresentCounter>) {
                state.destination = Counter{};
              } else if constexpr (std::is_same_v<V, PresentOther>) {
                state.destination = OtherScreen{};
              } else if constexpr (std::is_same_v<V, Dismiss>) {
                state.destination = std::nullopt;
              }
            },
            action);
      });
  feature.add(ifCaseLet<ParentState, ParentAction, Destination, Counter, CounterAction>(
      &ParentState::destination, caseState<Destination, Counter>(),
      casePath<ParentAction, Child, CounterAction>(&Child::action), counterBody()));
  return feature;
}

void testCaseStateRoundTrip() {
  auto path = caseState<Destination, Counter>();
  const Destination counter = Counter{5};
  expect(path.extract(counter).has_value() && path.extract(counter)->value == 5,
         "caseState: extracts the active alternative");
  expect(!path.extract(Destination{OtherScreen{}}).has_value(),
         "caseState: nullopt for a different alternative");
  const Destination embedded = path.embed(Counter{7});
  expect(std::holds_alternative<Counter>(embedded) && std::get<Counter>(embedded).value == 7,
         "caseState: embeds back into the variant");
}

void testRoutesOnlyWhenCasePresented() {
  TestStore<ParentState, ParentAction> store(ParentState{}, parentBody);

  // Present the counter, then increment it twice through the parent.
  store.send(PresentCounter{}, [](ParentState &s) { s.destination = Counter{}; });
  store.send(Child{Increment{}}, [](ParentState &s) { s.destination = Counter{1}; });
  store.send(Child{Increment{}}, [](ParentState &s) { s.destination = Counter{2}; });

  expect(!store.failed(), "ifCaseLet: routes to the child while presented");
}

void testNoOpWhenDismissedOrWrongCase() {
  TestStore<ParentState, ParentAction> store(ParentState{}, parentBody);

  // Nothing presented → child action is a no-op (no state change).
  store.send(Child{Increment{}}, {});

  // A different case presented → still a no-op for the counter child.
  store.send(PresentOther{}, [](ParentState &s) { s.destination = OtherScreen{}; });
  store.send(Child{Increment{}}, {});

  expect(!store.failed(), "ifCaseLet: no-op when dismissed or a different case is active");
}

} // namespace

int main() {
  testCaseStateRoundTrip();
  testRoutesOnlyWhenCasePresented();
  testNoOpWhenDismissedOrWrongCase();
  if (failures == 0) {
    std::println("All Navigation tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} Navigation test(s) failed.", failures);
  return 1;
}
