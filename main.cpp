#include <raylib.h>
#include <vector>
#include <string>
#include <variant>
#include <optional>
#include <algorithm>
#include <random>
#include <ctime>
#include <functional>
#include <cassert>
#include <format>

namespace Config {

constexpr float cardSize = 94.f;
constexpr float ui_height = 50.f;
constexpr int grid = 4;
constexpr int tileCount = grid * grid;
constexpr int emptyIndex = tileCount - 1; // last cell

}

static std::mt19937& rng() {
  static std::mt19937 g{std::random_device{}()};
  return g;
}

static int indexRow(int i) { return i / Config::grid; }
static int indexCol(int i) { return i % Config::grid; }

static bool adjacent(int i, int j) {
  const int r1 = indexRow(i), c1 = indexCol(i);
  const int r2 = indexRow(j), c2 = indexCol(j);
  return (r1 == r2 && std::abs(c1 - c2) == 1) || (c1 == c2 && std::abs(r1 - r2) == 1);
}

static std::ptrdiff_t findEmpty(const std::vector<std::string>& tiles) {
  const auto it = std::find_if(tiles.begin(), tiles.end(), [](const std::string& t){ return t.empty(); });
  return it != tiles.end() ? std::distance(tiles.begin(), it) : static_cast<std::ptrdiff_t>(-1);
}

static bool isSolved(const std::vector<std::string>& tiles) {
  for (int i = 0; i < Config::tileCount - 1; ++i) {
    if (tiles[i] != std::to_string(i + 1)) return false;
  }
  return tiles[Config::tileCount - 1].empty();
}

/// Count inversions for solvability check
static int inversionCount(const std::vector<int>& values) {
  int inv = 0;
  for (size_t i = 0; i < values.size(); ++i) {
    for (size_t j = i + 1; j < values.size(); ++j) {
      if (values[i] > values[j]) ++inv;
    }
  }
  return inv;
}

/// 15-puzzle solvability rules for even grid
static bool isSolvable(const std::vector<std::string>& labels) {
  std::vector<int> vals; vals.reserve(Config::tileCount - 1);
  int emptyRowFromBottom = 0;
  for (int i = 0; i < Config::tileCount; ++i) {
    if (labels[i].empty()) {
      // row counting from bottom (1-based)
      emptyRowFromBottom = Config::grid - indexRow(i);
    } else {
      vals.push_back(std::stoi(labels[i]));
    }
  }
  const int inv = inversionCount(vals);
  if (Config::grid % 2 == 1) {
    return (inv % 2) == 0; // odd grid
  } else {
    // even grid
    if ((emptyRowFromBottom % 2) == 0) {
      return (inv % 2) == 1;
    } else {
      return (inv % 2) == 0;
    }
  }
}

static std::vector<std::string> solvedTiles() {
  std::vector<std::string> tiles; tiles.reserve(Config::tileCount);
  for (int r = 0; r < Config::grid; ++r) {
    for (int c = 0; c < Config::grid; ++c) {
      const bool isEmpty = (r == Config::grid - 1 && c == Config::grid - 1);
      const std::string t = isEmpty ? std::string{} : std::to_string(r * Config::grid + c + 1);
      tiles.emplace_back(t);
    }
  }
  return tiles;
}

static void shuffleLabels(std::vector<std::string>& labels) {
  std::shuffle(labels.begin(), labels.end(), rng());
}

static std::vector<std::string> shuffledSolvable() {
  std::vector<std::string> labels = solvedTiles();
  // Keep shuffling until solvable and not already solved
  do { shuffleLabels(labels); } while (!isSolvable(labels) || isSolved(labels));
  return labels;
}

static std::vector<std::string> shuffledNearWin() {
    auto tiles = solvedTiles();
    int empty = findEmpty(tiles);
    std::vector<int> moves;
    for (int i = 0; i < static_cast<int>(tiles.size()); ++i) {
        if (adjacent(i, empty)) moves.push_back(i);
    }
    if (!moves.empty()) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(0, moves.size() - 1);
        std::swap(tiles[moves[dis(gen)]], tiles[empty]);
    }
    return tiles;
}

namespace Puzzle {

struct State {
    std::vector<std::string> tiles;
    bool isEnd = false;
    std::optional<double> startTime{}; // час старту гри
};

struct Shuffle {};
struct Move { int index; };
struct Restart {};
struct SetStartTime { double time; };
struct NearWinShuffle {};
struct Start {};

using Action = std::variant<Shuffle, Move, Restart, SetStartTime, NearWinShuffle, Start>;
struct StartTimer {};
using Effect = std::variant<StartTimer>;
using Dispatch = std::function<void(Action)>;

static auto reducer(const State& s, const Action& a) -> std::pair<State, std::vector<Effect>> {
    State ns = s;
    std::vector<Effect> effects;

    std::visit([&](auto&& act){
        using T = std::decay_t<decltype(act)>;
        if constexpr (std::is_same_v<T, Start>) {
          if (!s.startTime.has_value()) {
            effects.push_back(StartTimer{});
          }
        } else if constexpr (std::is_same_v<T, Shuffle>) {
            ns.tiles = shuffledSolvable();
        } else if constexpr (std::is_same_v<T, Move>) {
            const auto empty = findEmpty(ns.tiles);
            if (empty != -1 && act.index >= 0 && act.index < static_cast<int>(ns.tiles.size()) &&
                adjacent(act.index, static_cast<int>(empty))) {
                std::swap(ns.tiles[act.index], ns.tiles[static_cast<int>(empty)]);
            }
        } else if constexpr (std::is_same_v<T, Restart>) {
            ns.tiles = shuffledSolvable();
            ns.startTime = std::nullopt;
            ns.isEnd = false;
            effects.push_back(StartTimer{});
        } else if constexpr (std::is_same_v<T, SetStartTime>) {
            ns.startTime = act.time;
        } else if constexpr (std::is_same_v<T, NearWinShuffle>) {
            ns.tiles = shuffledNearWin();
        }
    }, a);

    ns.isEnd = isSolved(ns.tiles);
    if (ns.isEnd) ns.startTime = std::nullopt;

    return {ns, effects};
}

} // namespace Puzzle

template<typename State, typename Action, typename Effect>
struct Store {
  State state{};
  std::function<std::pair<State, std::vector<Effect>>(const State&, const Action&)> reducer;
  std::function<void(const std::vector<Effect>&, const Puzzle::Dispatch&)> effectRunner;

  Store(State s, decltype(reducer) r, decltype(effectRunner) er) : state(std::move(s)), reducer(std::move(r)), effectRunner(std::move(er)) {}
  void send(const Action& a) {
    auto [ns, effects] = reducer(state, a);
    state = ns;
    effectRunner(effects, [this](const Action& aa){ send(aa); });
  }
};

static Rectangle getRect(int i) {
  const int r = indexRow(i);
  const int c = indexCol(i);
  return {c * Config::cardSize, r * Config::cardSize, Config::cardSize, Config::cardSize};
}

static void drawCard(const std::string& text, Rectangle rect) {
  DrawRectangleRec(rect, BLACK);
  const Rectangle body{rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4};
  const Color bodyColor = text.empty() ? DARKPURPLE : ORANGE;
  DrawRectangleRec(body, bodyColor);
  if (!text.empty()) {
    const int fontSize = 50;
    const int textWidth = MeasureText(text.c_str(), fontSize);
    const int x = static_cast<int>(rect.x + (rect.width - textWidth) / 2);
    const int y = static_cast<int>(rect.y + (rect.height - fontSize) / 2);
    DrawText(text.c_str(), x, y, fontSize, BLACK);
  }
}

static void drawBoard(const std::vector<std::string>& tiles) {
    DrawRectangle(0, 0, Config::grid * Config::cardSize, Config::grid * Config::cardSize, DARKPURPLE);
    for (int i = 0; i < Config::tileCount; ++i) {
        drawCard(tiles[i], getRect(i));
    }
}

static void drawOverlay() {
  const int w = GetScreenWidth(), h = GetScreenHeight();
  DrawRectangle(0, 0, w, h, {0,0,0,192});
  const char* txt = "Victory!";
  const int fs = 60;
  DrawText(txt, (w - MeasureText(txt, fs))/2, (h - fs)/2 - 32, fs, WHITE);
  const char* desc = "Click or press R to continue.";
  DrawText(desc, (w - MeasureText(desc, 20))/2, (h + fs)/2, 20, WHITE);
}

int main() {
  SetConfigFlags(FLAG_VSYNC_HINT);
  InitWindow(static_cast<int>(Config::grid * Config::cardSize), static_cast<int>(Config::grid * Config::cardSize + Config::ui_height), "15 Puzzle");

  auto effectRunner = [](const std::vector<Puzzle::Effect>& effects, const Puzzle::Dispatch& dispatch) {
    for (const auto& eff : effects) {
      std::visit([&](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, Puzzle::StartTimer>) {
          dispatch(Puzzle::SetStartTime{GetTime()});
        }
      }, eff);
    }
  };

  Puzzle::State init{shuffledSolvable(), false, GetTime() - 10000};
  Store<Puzzle::State, Puzzle::Action, Puzzle::Effect> store(init, Puzzle::reducer, effectRunner);
  store.send(Puzzle::Start{});

  while (!WindowShouldClose()) {
    const auto& s = store.state;

    if (IsKeyPressed(KEY_R)) store.send(Puzzle::Restart{});

    if (!s.isEnd) {
      if (IsKeyPressed(KEY_S)) store.send(Puzzle::Shuffle{});
      
      const Vector2 m = GetMousePosition();
      if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        for (int i = 0; i < static_cast<int>(s.tiles.size()); ++i) {
          if (CheckCollisionPointRec(m, getRect(i))) { store.send(Puzzle::Move{i}); break; }
        }
      }

      const auto empty = findEmpty(s.tiles);
      if (empty != -1) {
        const int r = static_cast<int>(empty) / Config::grid;
        const int c = static_cast<int>(empty) % Config::grid;
        if (IsKeyPressed(KEY_UP)    && r < Config::grid - 1) store.send(Puzzle::Move{(r+1)*Config::grid + c});
        if (IsKeyPressed(KEY_DOWN)  && r > 0)                store.send(Puzzle::Move{(r-1)*Config::grid + c});
        if (IsKeyPressed(KEY_LEFT)  && c < Config::grid - 1) store.send(Puzzle::Move{r*Config::grid + (c+1)});
        if (IsKeyPressed(KEY_RIGHT) && c > 0)                store.send(Puzzle::Move{r*Config::grid + (c-1)});
      }
      
      static double lastWTime = 0.0;
      const double doublePressThreshold = 0.4;

      if (IsKeyPressed(KEY_W)) {
          double now = GetTime();
          if (now - lastWTime < doublePressThreshold) {
              store.send(Puzzle::NearWinShuffle{});
              lastWTime = 0;
          } else {
              lastWTime = now;
          }
      }
    } else {
      if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) store.send(Puzzle::Restart{});
    }

    BeginDrawing();
    ClearBackground(BLACK);
    drawBoard(store.state.tiles);

    int totalSeconds = 0;
    if (store.state.startTime.has_value()) {
        totalSeconds = static_cast<int>(GetTime() - *store.state.startTime);
    }

    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    std::string timeStr = std::format("{:02}:{:02}:{:02}", hours, minutes, seconds);

    const int fs = 30;
    DrawText(timeStr.c_str(), 16, GetScreenHeight() - fs - 10, fs, WHITE);

    if (store.state.isEnd) drawOverlay();
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
