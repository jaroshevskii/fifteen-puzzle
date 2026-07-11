# itch.io page — copy/paste content

Ready-to-paste values for the itch.io "Create a new project" form, plus the
page description. Adjust names/links to taste.

## Form fields

- **Title:** `15 Puzzle`
- **Project URL:** `fifteen-puzzle`
- **Short description / tagline:** `The classic sliding puzzle — race a friend and climb the leaderboards.`
- **Classification:** Games
- **Kind of project:** Downloadable
- **Release status:** Released
- **Pricing:** Free (or "$0 or donate" if you want optional donations)
- **Genre:** Puzzle
- **Language:** English (select it — don't leave blank and don't pick every language; the UI is English).
- **Multiplayer support** (Metadata → set the classification, don't use a "multiplayer" tag):
  - enable multiplayer, then check **Server-based networked multiplayer** (a
    player hosts the bundled `FifteenServer`) **and** **Ad-hoc / LAN networked
    multiplayer** (LAN / Radmin / Hamachi / Tailscale). Do **not** check "Local
    multiplayer" (no same-device play).
  - **Player count: 1–2** (single-player supported; races are 1v1).
  - Setting this is why the tags below correctly omit "multiplayer".
- **Tags** (max 10; suggested, relevant, no genre/platform/metadata/name/synonym words): `sliding-puzzle`, `leaderboards`, `minimalist`, `retro`, `arcade`, `open-source`, `speedrun`
- **AI generation disclosure:** **Yes** — see note below.
- **Custom noun:** leave blank (defaults to "game")
- **Community:** Comments (optional)
- **Cover image:** upload `Bootstrap/cover.png` (630×500, ready to use; source `Bootstrap/cover.svg`).
- **Screenshots:** upload the 5 in `Bootstrap/screenshots/` (`01-gameplay`,
  `02-multiplayer`, `03-live-games`, `04-leaderboard`, `05-victory`). They're
  representative renders faithful to the real UI (same palette/fonts); swap for
  raw in-game captures anytime.

### Theme — banner & background

itch has no dedicated "banner" field, so use these two:

- **Banner** (`Bootstrap/banner.png`, 1920×480): paste it as the **first image
  at the top of the Description** editor — it becomes the page's hero strip.
- **Background** (`Bootstrap/background.png`, 1920×1200): on the saved project
  page click **Edit theme** → **Background** → upload it, set **image position:
  cover** and **no repeat**; set the theme **background color** to `#0a0810`
  (matches the image edges) and keep light body text. It's intentionally dark
  with the motif at the edges, so the centred content column stays readable.

### Uploads (mark platform per file; "This file will be run in the browser": OFF)

- `FifteenPuzzle-macos-arm64.tar.gz` → platform **macOS**
- `FifteenPuzzle-linux-x86_64.tar.gz` → platform **Linux**
- `FifteenPuzzle-windows-x86_64.zip` → platform **Windows**

Upload the files directly to itch (butler does this — see `release.yml`); don't
just link to GitHub Releases. Select **only** these three platforms — they're
real native executables we build and test.

### AI disclosure note — answer **Yes**

itch's definition of generative AI explicitly includes large language models
(ChatGPT-like). This project's **code and the vector icon/cover were produced
with LLM assistance**, so the honest, guidelines-compliant answer is **Yes,
this project contains the output of Generative AI**. It only adds the game to
the "AI Assisted" browse page — it is not a penalty.

What is *not* AI-generated (state this in the description for clarity): there
are **no image-model (DALL-E/Midjourney/Stable Diffusion) raster assets and no
AI-generated audio**; the graphics are hand-written vector art, audio is
procedural, and the game logic (solver, shuffle, referee) is classic
algorithms, not generative AI. Disclose anyway — LLM-written code/art counts.

---

## Description (paste into the Description editor)

**15 Puzzle** is the classic 4×4 sliding puzzle, rebuilt as a small but complete
game: slide the tiles back into order as fast as you can, then climb the
leaderboards — solo, or head-to-head against another player in real time.

### Features

- 🧩 **Sliding puzzle, 4×4 up to 13×13** — pick your difficulty with the number
  keys.
- ⏱️ **Timer, move counter and auto-solver** — press **H** to watch it solve
  itself.
- 🏆 **Leaderboards** — local by default; online when you connect to a server
  (the game ships with one you can host — no account, no always-on service).
- ⚔️ **Realtime multiplayer races** — you and an opponent get the *same* board;
  first to solve wins. Watch a live mini-preview of their board as they play.
  (Peer-hosted: one player runs the included server — see "Playing together".)
- 📡 **Live Games feed** — see who's playing on that server and which races are
  happening right now.
- ✨ Sliding-tile animation, a 3-2-1 countdown before each race, and a little
  victory confetti.

### Controls

- Move a tile: **mouse click** or **arrow keys**
- Resize the board: **0** (4×4) … **9** (13×13)
- Shuffle: **S** · Restart: **R** · Auto-solve: **H** · Sound: **M**
- Menus: arrow keys / mouse, **Enter** to select, **Esc** to go back

### Playing together (multiplayer & online leaderboard)

Multiplayer and the online leaderboard run through a small server that ships
**inside the download** (`FifteenServer` + `run-server.sh`). Single-player and
the local leaderboard work offline with no setup.

**To race a friend:**

1. **One player hosts.** Extract the archive and start the server:
   - macOS / Linux: `./run-server.sh`
   - Windows: run `FifteenServer.exe`

   It listens on port **8080** (leaderboard) and **8091** (multiplayer).

2. **The other player connects to the host's IP.**
   - **Same Wi-Fi / LAN?** Use the host's local IP directly.
   - **Over the internet?** The simplest way is a virtual-LAN app so both
     machines share one private network — **Radmin VPN** or **Hamachi**
     (classic, free), or the more modern **Tailscale** / **ZeroTier**. Install
     it on both PCs, join the same network, and use the host's virtual IP.

3. **Point the game at the host** by setting two environment variables before
   launching (replace `HOST_IP` with the host's LAN / Radmin / Hamachi /
   Tailscale IP):

   - macOS / Linux:
     ```sh
     FIFTEEN_API_BASE_URL=http://HOST_IP:8080 FIFTEEN_MP_HOST=HOST_IP \
       ./FifteenPuzzle.app/Contents/MacOS/FifteenPuzzle      # macOS
     FIFTEEN_API_BASE_URL=http://HOST_IP:8080 FIFTEEN_MP_HOST=HOST_IP \
       ./FifteenPuzzle                                       # Linux
     ```
   - Windows (Command Prompt):
     ```bat
     set FIFTEEN_API_BASE_URL=http://HOST_IP:8080
     set FIFTEEN_MP_HOST=HOST_IP
     FifteenPuzzle.exe
     ```

   The host can just launch the game normally (it defaults to `localhost`).
   Then both pick **Multiplayer** from the menu — you'll be matched and the race
   begins. (If the ports are firewalled, allow **8080** and **8091** on the
   host, or forward them.)

> The host must keep `FifteenServer` running for the duration of the session.
> Without a reachable server, the game still plays fine solo and keeps a local
> leaderboard.

### Platform notes

- **macOS:** the app is **not code-signed**, so Gatekeeper will warn on first
  launch. Right-click the app → **Open** (then confirm), or run
  `xattr -dr com.apple.quarantine FifteenPuzzle.app` once.
- **Windows:** SmartScreen may warn (unsigned). If it won't start, install the
  latest **Microsoft Visual C++ Redistributable**.
- **Linux:** needs a recent `libcurl` (present on most desktops).

### Made with

Open source (MIT), game **and** server:
<https://github.com/jaroshevskii/fifteen-puzzle>. Built with AI-assisted
tooling (code and the vector art were produced with LLM help); there are no
image-model or AI-generated audio assets.

---

## Guidelines compliance checklist

Checked against itch.io's *Content creator quality guidelines*:

- ✅ **Page ready before publishing** — keep it **Draft** until all three
  platform files + cover + screenshots + metadata are in; first publish drops
  it onto "Most Recent" once, so publish it polished.
- ✅ **Accurate metadata** — Games / Downloadable / Released; Genre = Puzzle;
  set the **Multiplayer support** classification (server-based, 2 players)
  instead of a multiplayer tag.
- ✅ **Platforms** — select only macOS + Linux + Windows (real native builds we
  test); nothing else.
- ✅ **Tags** — relevant/suggested only; no genre ("puzzle"), platform, name
  ("15-puzzle"), synonym, or metadata ("multiplayer") tags.
- ✅ **Language** — English selected (not blank, not "all").
- ✅ **Cover** — `Bootstrap/cover.png`, static (no seizure-inducing flashing).
- ✅ **Screenshots** — 5 provided in `Bootstrap/screenshots/` (gameplay,
  multiplayer, live games, leaderboard, victory), faithful to the real UI.
  Regenerate with `python3 Bootstrap/make-screenshots.py` + `rsvg-convert`.
- ✅ **Not misleading** — online play is described as peer-hosted/LAN, not a
  hosted service; every listed feature exists and works.
- ✅ **Generative-AI disclosure** — answered **Yes** (LLM-assisted code/art).
- ✅ **Files uploaded directly** (butler), not just a store/GitHub link.
- ✅ **No adult content, ads, third-party logins, reskins, or key-only upload.**
- ✅ **Not a jam-spam / sale-manipulation** case (free, first release).
