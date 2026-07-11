# Release Notes

## v0.3.0 — Monorepo, Server & Realtime Multiplayer

The biggest release yet: 15 Puzzle becomes a **client + server monorepo** in the
style of [isowords][isowords], gains a **realtime multiplayer** mode with a live
spectator feed, and picks up a wave of architecture and polish work. Every
platform release now ships **both** the game and its server.

### Highlights

- 🎮 **Realtime head-to-head multiplayer.** Queue for an opponent, get dealt the
  identical board, and race. The server is the referee: it deals every board
  from a shared seed and re-plays every reported move on its own copy — a client
  can't fake a win. A live mini-preview of the opponent's board and a 3-2-1-GO
  countdown make the race feel alive.
- 📡 **Live Games feed.** Watch what's happening on the server in real time —
  online / racing / queued counts, games in progress, and a rolling ticker of
  recent finishes. The groundwork for full board-spectating.
- 🖥️ **A real server (`FifteenServer`).** A self-contained binary (SQLite next
  to it, no external database) serving the leaderboard HTTP API and the
  multiplayer referee. Now packaged into every release archive alongside the
  game, with `Bootstrap/run-server.sh`.
- 🏆 **Elo ratings core** for competitive play, ready to wire into ranked
  matchmaking.
- ✨ **Juice:** sliding-tile animation, a victory confetti burst, and the race
  countdown.

### Multiplayer

- Matchmaking by board size; both players receive the same
  `PuzzleCore::scrambled(grid, seed)` board.
- Server-authoritative refereeing: illegal moves are rejected, the win is
  detected server-side, and only verified results reach the leaderboard.
- Opponent's live board preview (their referee-validated moves replayed
  locally), move-count HUD, walkover handling, and instant rematch (`R`/`Enter`).
- `MultiplayerFeature` is a fully `TestStore`-tested state machine; the
  connection runs as a cancellable `store.addTask` that sends a polite `Leave`
  on teardown.

### Live Games (spectator feed)

- New shared protocol messages over the same socket: `Observe`, `Presence`,
  `MatchStarted`, `MatchEnded`.
- Subscribing streams a snapshot of every match already in progress, then live
  updates. `LiveFeature` folds them into counts, an in-progress list (de-duped
  by match id) and a capped recent-finish ticker — exhaustively tested with a
  scripted observer stub.

### Server

- **Hardening:** finished worker threads are reaped instead of accumulating, and
  a connection cap (`FIFTEEN_SERVER_MAX_CONN`, default 256) rejects over-cap
  clients with a typed `ServerFull` message.
- **Shared routing/rules/protocol** defined once and used by both sides:
  `ServerRouter` (HTTP), `PuzzleCore` (board rules), `MultiplayerCore` (socket).
- **Env vars:** `FIFTEEN_SERVER_PORT` (8080), `FIFTEEN_SERVER_MP_PORT` (8091),
  `FIFTEEN_SERVER_MAX_CONN` (256), `FIFTEEN_SERVER_DATABASE`.
- Client targets it via `FIFTEEN_API_BASE_URL` / `FIFTEEN_MP_HOST` /
  `FIFTEEN_MP_PORT`; unreachable → local-only leaderboard and a retryable
  multiplayer screen.

### Architecture (Point-Free / TCA)

- **`onChange` trigger** ported to `Feature` (TCA 2.0 style): the win edge is now
  declarative, replacing the manual `didSubmitCurrentWin` flag.
- **`RatingCore`** — a pure, shared, property-tested Elo module (expected score,
  K-factor schedule, `applyWin`/`project`, ranks, seasonal reset).
- isowords-style **in-process integration tests**: client features run against
  the real server middleware with no sockets.

### Effects & animation

- Sliding tiles (eased interpolation) across the solo board.
- Victory confetti behind the win overlay.
- Multiplayer race countdown (3-2-1-GO) with input gated until GO.

### CI / release

- Release archives now contain `FifteenPuzzle`, `FifteenServer` and
  `run-server.sh` per platform.
- macOS ships as a proper `FifteenPuzzle.app` bundle with an icon
  (`Bootstrap/make-macos-app.sh` + `Bootstrap/icon.svg`), so it launches from
  Finder and the itch.io app.
- Optional **itch.io auto-publish**: set repo variables `ITCH_USER` /
  `ITCH_GAME` and the `BUTLER_API_KEY` secret, and the release workflow pushes
  each platform to its itch channel via butler. See `docs/itch-page.md` for the
  store page copy (including how to play multiplayer over Radmin VPN / Hamachi /
  Tailscale / ZeroTier).
- New `Bootstrap/e2e.py` end-to-end smoke test (HTTP API + a scripted two-client
  race) runs against the real server binary on macOS and Linux CI.

### Tests

15 suites, all green: the existing feature/navigation/persistence coverage plus
`ServerRouterTests`, `SiteMiddlewareTests`, `GameServerTests` (incl. the live
feed), `MultiplayerFeatureTests`, `LiveFeatureTests`, and `RatingCoreTests`.

### Notes & known issues

- **Toolchain:** one feature TU is pinned to `-O3` on Clang/GCC — the
  bleeding-edge Clang + `import std` toolchain miscompiles it at
  RelWithDebInfo's `-O2` (a spurious stack-canary abort; clean at `-O3` and
  under ASan/UBSan). This extends the existing `-O0` guard documented in
  `CMakeLists.txt`.
- Windows CI runs the HTTP smoke only; the Winsock TCP race script is a
  follow-up.
- `RatingCore` and full board-spectating are implemented/seeded but not yet
  wired into the gameplay loop — see `ROADMAP.md` for the staged next waves
  (daily challenge, replays, reconnect, ranked matchmaking, campaign, …).

[isowords]: https://github.com/pointfreeco/isowords
