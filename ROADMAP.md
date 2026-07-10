# Roadmap

## Shipped in v0.3.0 (see `RELEASE_NOTES.md`)

- **Phase 1** — server packaged into every release + `Bootstrap/e2e.py` in CI;
  server hardening (worker reaping, connection cap + typed `ServerFull`). ✅
- **Phase 2.5-ish (socket)** — live-games feed / spectator-lite (`Observe` /
  `Presence` / `MatchStarted` / `MatchEnded`, `LiveFeature`). ✅ Full board
  spectating, reconnect and the room lobby remain (Phase 2.1/2.2).
- **Phase 3.4 (partial)** — `RatingCore` (Elo) shared module + tests; wiring
  ranked matchmaking remains. ✅ core
- **Phase 4.2** — `onChange` trigger ported and used for the win edge. ✅
- **Phase 5 (partial)** — sliding tiles, victory confetti, race countdown. ✅

Everything below is still to do.

---

The next waves of work, in dependency order. Each phase lands independently and
keeps the invariants that define this codebase: **routes/rules/protocol defined
once and shared**, **the server verifies rather than trusts**, **every feature
is an exhaustively-testable state machine**, and **all I/O behind Dependency
keys**.

---

## Phase 1 — Ship the server with every release

**Goal:** every platform archive contains both `FifteenPuzzle` and
`FifteenServer`, and CI proves they talk to each other before publishing.

1. **Release matrix packs both binaries.** `release.yml` builds the default
   target set (already includes `FifteenServer`) with `FIFTEEN_STATIC_DEPS=ON`
   and packs `FifteenPuzzle` + `FifteenServer` + `Bootstrap/run-server.sh`
   into one archive per platform (`…-macos-arm64.tar.gz`, `…-linux-x86_64.tar.gz`,
   `…-windows-x86_64.zip`).
   - Server needs no raylib/OpenAL: static SQLite amalgamation makes it fully
     self-contained; verify the static path skips the GUI-only deps for a
     hypothetical future server-only build.
2. **E2E smoke test in CI** (macOS + Linux jobs): boot `FifteenServer` on
   scratch ports, `curl` submit → fetch → 400 → 404, then a scripted two-client
   TCP race (join → same seed → relay → walkover) — promote the ad-hoc Python
   script already used during development to `Bootstrap/e2e.py` and run it as a
   workflow step. Windows job: HTTP smoke only at first (winsock race script as
   a follow-up).
3. **Server hardening before it ships:** reap finished worker threads in
   `GameServer::run` (today the `workers` vector grows forever), add a
   connection cap (reject with a typed message instead of unbounded threads),
   and log accepted/refused counts.

**Done when:** a release run publishes archives where `./FifteenServer` boots
and the packaged client plays multiplayer against it out of the box.

---

## Phase 2 — Top 5 socket features (realtime surface)

Ranked by wow-per-effort. All extend the one shared `MultiplayerCore` protocol,
so client and server can never drift.

### 2.1 Live lobby + spectator mode ⭐ flagship
Watch any game in progress, live. The client already replays boards from
relayed moves — a spectator is *two* opponent-previews.
- Protocol: `ListRooms` → `RoomList{[{id, players, grid, moveCounts, elapsed}]}`,
  `Watch{roomId}` → `WatchStarted{seed, grid, names, moveHistories}` (late-join
  snapshot!), then relayed `SpectatedMove{player, index}`; `Unwatch`.
- Server: room registry with ids, spectator fan-out list per room (spectators
  get moves but cannot send them), snapshot-on-join built by replaying stored
  histories — verification data we already keep.
- Client: `LobbyFeature` (IdentifiedArray of rooms, auto-refresh over the
  socket) + `SpectateFeature` (two live boards, both driven by the same replay
  logic as the opponent preview).

### 2.2 Reconnect & resume
Drop the Wi-Fi mid-race and get back in.
- Protocol: `Start` gains a `sessionToken`; new `Rejoin{token}` →
  `RejoinAccepted{seed, grid, yourHistory, opponentHistory, elapsed}` or
  `RejoinExpired`.
- Server: on disconnect, keep the room alive for a grace window (~30s,
  clock via the Date dependency — testable) before declaring a walkover.
- Client: connection loop retries with backoff; `Phase::reconnecting` with UI.
  The board restores by replaying histories — no new sync machinery.

### 2.3 N-player races (rooms of 2–8)
Generalize `Room` from a pair to a vector of boards; everyone races the same
seed; live standings by move count; first solved wins, rest ranked by
progress-at-finish.
- Engine change is mostly `std::array<PlayerId,2>` → vector + standings calc;
  the referee logic per board is untouched.
- Client: standings strip of mini previews (the Phase 2.1 spectator widget,
  reused).

### 2.4 In-race chat & emotes
Six canned emotes + short text, relayed through the room, rate-limited
server-side (token bucket per connection — pure Engine logic, testable with the
pinned clock).

### 2.5 Presence & live ticker
Menu becomes alive: `Hello` → periodic `Presence{online, racing, watching}`
push + event ticker (`"Ada solved 7×7 in 2:04"` from verified results).
- One lightweight subscription channel on the existing socket; a
  `PresenceFeature` scoped into the main menu destination.

**Order:** 2.1 → 2.2 → 2.5 → 2.3 → 2.4 (lobby infrastructure unlocks the rest).

---

## Phase 3 — Top 5 game mechanics (production plan)

### 3.1 Daily Challenge (the isowords signature) ⭐ flagship
One deterministic board per date, global daily leaderboard, streaks.
- Server: seed = HMAC(date, server secret) so it cannot be precomputed;
  routes `GET /daily` (today's grid + seed issued only once per player) and
  `POST /daily/scores` with **full move-history submission that the server
  replays with PuzzleCore** before accepting — real isowords verification,
  now for solo play too.
- Schema: `daily_games(date, player, moves, duration, verified)` + streaks.
- Client: `DailyChallengeFeature` (locked until midnight after solving —
  Date dependency makes the countdown testable), calendar of past results.

### 3.2 Attack modifiers in multiplayer (battle mode)
Completing a row/column charges an attack that scrambles 2 random tiles on the
opponent's board. Server-authoritative: the Engine detects the completed line
on *its* copy, applies the scramble to the victim's board with the room RNG,
and broadcasts `AttackApplied{cells}` to both (previews stay in sync because
they replay server messages, not local guesses).

### 3.3 Campaign with constraints
50 hand-tuned levels: move budgets, time limits, fog-of-war (only tiles
adjacent to the hole visible), mirrored controls, checkpoint boards.
- Pure `PuzzleCore` extensions (constraint = predicate over State) + a
  `CampaignFeature` with progression persisted via Sharing; stars synced to the
  server when online (route reuses ScoreSubmission shape + level id).

### 3.4 Elo rating & seasons
Verified multiplayer results feed an Elo update (K-factor by games played);
matchmaking prefers ±150 rating window with widening timeout; seasonal reset
with ranks (Bronze→Grandmaster).
- All rating math in a shared `RatingCore` module — client shows projected
  ±Δ before the race, server applies the same function; property-tested.

### 3.5 Replays & shared games
Every verified game already has (seed, move history) — a replay *is* the
verification data. `GET /replays/{code}` + a `ReplayFeature` with play/pause/
scrub (timeline slider maps to history prefix — deterministic by construction).
Share codes after a race ("watch my 13×13 run").

**Production order & why:** 3.1 (retention, reuses everything) → 3.5 (free —
data already exists) → 3.4 (needs volume from 3.1) → 3.2 → 3.3.
Each ships with: migration (`PRAGMA user_version` bump), route/protocol
addition in the shared modules, feature + view, TestStore suite, middleware/
engine integration tests, README section.

---

## Phase 4 — Deeper Point-Free patterns (the architecture flex)

Ports that visibly pay off in this codebase, in impact order:

1. **CustomDump for TestStore failures.** Today a mismatch prints
   "state did not match expectation" with no detail. Port `customDump` +
   `diff`: recursive pretty-printer over structs/variants/optionals (C++26
   reflection groundwork; until then, a `CustomDumpable` customization point
   implemented per State) and print a `-expected/+actual` diff. This is the
   single biggest DX win available. (pfw-custom-dump)
2. **`onChange` trigger modifier.** The win-edge in `AppFeature` is manual
   flag-juggling (`didSubmitCurrentWin`). Port TCA's `.onChange(of:)`:
   `feature.onChange([](const State &s){ return s.puzzle.isGameOver; },
   [](old, new, state, store){ … })` — declarative edge detection, reused for
   autosave throttling too. (pfw-composable-architecture 2.0 triggers)
3. **Delegate closures for child→parent.** `MultiplayerFeature` finishing, or
   Settings edits, currently leak upward by the parent inspecting child state
   on `Dismiss`. Port TCA 2.0 delegate closures: parent installs
   `onFinished`/`onSettingsChanged` when scoping the child. (tca 2.0)
4. **IdentifiedCollections.** `IdentifiedArray<Id, T>` port for the lobby's
   room list and N-player standings — O(1) id lookup with stable order,
   `forEach`-style scoping of per-row features. (pfw-identified-collections)
5. **Snapshot tests of state flows.** Golden-file dumps (via #1's customDump)
   of full State after scripted action sequences — `Tests/__Snapshots__/…` with
   a record mode env var, the pfw-snapshot-testing workflow. Catches
   regressions TestStore assertions don't spell out. (pfw-debug-snapshots)
6. **`reportIssue` / IssueReporting.** Soft-assert unexpected-but-recoverable
   states (a `MoveRejected` arriving, a late action after dismiss): test
   context → TestStore failure; live → stderr warning once. (pfw-issue-reporting)
7. **`_printChanges` debugging modifier.** Opt-in per-feature action + state
   diff logging (uses #1), toggled by an env var.

---

## Phase 5 — Juice: effects & animation

View-layer only (raylib clocks), never feature state — same discipline as the
intro animation.

1. **Sliding tiles.** Tiles glide (120ms ease-out) instead of teleporting —
   view keeps a per-cell render-offset map keyed by (index → previous rect),
   decaying every frame. Applies to solo, multiplayer, opponent preview and
   future spectator boards for free.
2. **Race start countdown.** 3-2-1-GO overlay with scale+fade per digit and
   beeps; input gated until GO (view suppresses `TileTapped` collection —
   feature stays untouched).
3. **Win celebration.** Confetti particle burst + tile cascade wave from the
   last-moved cell; victory text springs in. Walkover gets a subtler variant.
4. **Ambient polish.** Hover lift + press depression on tiles, hole inner glow,
   button hover transitions in MenuView, screen shake on (future) attacks,
   opponent-preview pulse on each relayed move.
5. **Screen transitions.** Cross-fade between destinations (menu ↔ game ↔
   leaderboard) driven by destination-change detection in the view; pause menu
   slides over a blurred board.
6. **Sound pass.** Distinct slide/deny/win/lose/countdown/attack samples through
   the existing AudioPlayerClient (new `Sound` cases + OpenAL buffers).

---

## Suggested execution order

| Wave | Contents | Rationale |
|---|---|---|
| 1 | Phase 1 (CI + hardening) | Unblocks shipping everything else |
| 2 | Phase 4.1–4.3 (customDump, onChange, delegates) | Makes all later features cheaper & safer to build |
| 3 | Phase 2.1 + 2.2 (lobby/spectate + reconnect) | Flagship realtime surface |
| 4 | Phase 3.1 + 3.5 (daily challenge + replays) | Retention loop on verified data |
| 5 | Phase 5 (juice) + Phase 2.5 (presence) | The game *feels* alive |
| 6 | Phase 3.4 / 3.2 / 2.3 / 2.4 / 3.3 | Competitive depth, then breadth |
