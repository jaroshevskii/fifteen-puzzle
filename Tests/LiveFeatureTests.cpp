// Tests LiveFeature with the TestStore and a scripted observer stub: presenting
// dispatches Appeared, whose observe task (run inline) streams a Presence
// snapshot, MatchStarted / MatchEnded feed messages, and a connection failure —
// covering the counts, the in-progress list (with de-dupe), the recent-results
// ticker cap, and the failure path.

import std;
import ComposableArchitecture;
import LiveFeature;
import MultiplayerClient;
import MultiplayerCore;

using ComposableArchitecture::TestStore;
using Dependencies::DependencyContext;
using Dependencies::DependencyValues;
using Dependencies::withDependencies;

namespace {

int failures = 0;
void expect(bool ok, std::string_view msg) {
  if (!ok) {
    ++failures;
    std::println(std::cerr, "FAIL: {}", msg);
  }
}

MultiplayerClient::Client observerStub(std::vector<MultiplayerClient::Event> events) {
  MultiplayerClient::Client client;
  client.observe = [events](std::function<void(MultiplayerClient::Event)> onEvent,
                            std::stop_token) {
    for (const auto &event : events) {
      onEvent(event);
    }
  };
  return client;
}

MultiplayerClient::Event received(MultiplayerCore::ServerMessage message) {
  return MultiplayerClient::Received{std::move(message)};
}

void testFeedFoldsPresenceAndMatches() {
  withDependencies(
      [](DependencyValues &values) {
        values.context = DependencyContext::test;
        values.set<MultiplayerClient::Key>(observerStub({
            MultiplayerClient::Connected{},
            received(MultiplayerCore::Presence{.online = 3, .racing = 2, .waiting = 0}),
            received(MultiplayerCore::MatchStarted{
                .matchId = 1, .gridSize = 4, .playerA = "Ada", .playerB = "Bob"}),
            received(MultiplayerCore::MatchStarted{
                .matchId = 1, .gridSize = 4, .playerA = "Ada", .playerB = "Bob"}), // dup
            received(MultiplayerCore::MatchEnded{
                .matchId = 1, .winnerName = "Ada", .gridSize = 4, .durationSeconds = 63}),
        }));
      },
      [] {
        TestStore<LiveFeature::State, LiveFeature::Action> store(LiveFeature::initialState(),
                                                                 LiveFeature::body);

        store.send(LiveFeature::Appeared{}, {});

        store.receive([](LiveFeature::State &state) { state.phase = LiveFeature::Phase::live; });
        store.receive([](LiveFeature::State &state) {
          state.online = 3;
          state.racing = 2;
        });
        store.receive([](LiveFeature::State &state) {
          state.matches.insert(
              state.matches.begin(),
              LiveFeature::Match{.matchId = 1, .gridSize = 4, .playerA = "Ada", .playerB = "Bob"});
        });
        store.receive({}); // duplicate MatchStarted — de-duped, no change
        store.receive([](LiveFeature::State &state) {
          state.matches.clear();
          state.recent.insert(state.recent.begin(),
                              LiveFeature::FinishedMatch{
                                  .winnerName = "Ada", .gridSize = 4, .durationSeconds = 63});
        });

        expect(!store.failed(), "feed: state transitions match");
        expect(store.state().matches.empty(), "feed: finished match removed from in-progress");
        expect(store.state().recent.size() == 1 && store.state().recent.front().winnerName == "Ada",
               "feed: finished match added to the ticker");
        return 0;
      });
}

void testConnectionFailure() {
  withDependencies(
      [](DependencyValues &values) {
        values.context = DependencyContext::test; // Key testValue's observe fails immediately
      },
      [] {
        TestStore<LiveFeature::State, LiveFeature::Action> store(LiveFeature::initialState(),
                                                                 LiveFeature::body);
        store.send(LiveFeature::Appeared{}, {});
        store.receive([](LiveFeature::State &state) { state.phase = LiveFeature::Phase::failed; });
        expect(!store.failed(), "failure: connect failure surfaces");
        expect(store.state().phase == LiveFeature::Phase::failed, "failure: phase is failed");
        return 0;
      });
}

} // namespace

int main() {
  testFeedFoldsPresenceAndMatches();
  testConnectionFailure();

  if (failures == 0) {
    std::println("All LiveFeature tests passed.");
    return 0;
  }
  std::println(std::cerr, "{} LiveFeature test(s) failed.", failures);
  return 1;
}
