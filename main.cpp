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

namespace Config {

constexpr float cardSize = 94.f;
constexpr int grid = 4;
constexpr int tileCount = grid * grid;
constexpr int emptyIndex = tileCount - 1; // last cell

}

struct Card {
  std::string text;
  Rectangle rect{};

  Card() = default;
  Card(std::string t, Rectangle r) : text(std::move(t)), rect(r) {}

  void draw() const {
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
};

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

static std::ptrdiff_t findEmpty(const std::vector<Card>& cards) {
  const auto it = std::find_if(cards.begin(), cards.end(), [](const Card& c){ return c.text.empty(); });
  return it != cards.end() ? std::distance(cards.begin(), it) : static_cast<std::ptrdiff_t>(-1);
}

static bool isSolved(const std::vector<Card>& cards) {
  for (int i = 0; i < Config::tileCount - 1; ++i) {
    if (cards[i].text != std::to_string(i + 1)) return false;
  }
  return cards[Config::tileCount - 1].text.empty();
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

static std::vector<Card> makeGrid() {
  std::vector<Card> cards; cards.reserve(Config::tileCount);
  for (int r = 0; r < Config::grid; ++r) {
    for (int c = 0; c < Config::grid; ++c) {
      const bool isEmpty = (r == Config::grid - 1 && c == Config::grid - 1);
      const std::string t = isEmpty ? std::string{} : std::to_string(r * Config::grid + c + 1);
      cards.emplace_back(t, Rectangle{c * Config::cardSize, r * Config::cardSize, Config::cardSize, Config::cardSize});
    }
  }
  return cards;
}

static void shuffleLabels(std::vector<std::string>& labels) {
  std::shuffle(labels.begin(), labels.end(), rng());
}

static std::vector<Card> shuffledSolvable(const std::vector<Card>& base) {
  std::vector<std::string> labels; labels.reserve(base.size());
  for (const auto& c : base) labels.push_back(c.text);
  // Keep shuffling until solvable and not already solved
  do { shuffleLabels(labels); } while (!isSolvable(labels) || isSolved([&]{
    std::vector<Card> tmp = base; for (size_t i = 0; i < tmp.size(); ++i) tmp[i].text = labels[i]; return tmp; }()));
  
  std::vector<Card> out = base;
  for (size_t i = 0; i < out.size(); ++i) out[i].text = labels[i];
  return out;
}

namespace Puzzle {

struct State { std::vector<Card> cards; bool isEnd = false; };
struct Shuffle {}; struct Move { int index; }; struct Restart {};
using Action = std::variant<Shuffle, Move, Restart>;

static State reducer(const State& s, const Action& a) {
  State ns = s;

  std::visit([&](auto&& act){
    using T = std::decay_t<decltype(act)>;
    if constexpr (std::is_same_v<T, Shuffle>) {
      ns.cards = shuffledSolvable(ns.cards);
    } else if constexpr (std::is_same_v<T, Move>) {
      const auto empty = findEmpty(ns.cards);
      if (empty != -1 && act.index >= 0 && act.index < static_cast<int>(ns.cards.size()) && adjacent(act.index, static_cast<int>(empty))) {
        std::swap(ns.cards[act.index].text, ns.cards[static_cast<int>(empty)].text);
      }
    } else if constexpr (std::is_same_v<T, Restart>) {
      ns.cards = shuffledSolvable(makeGrid());
      ns.isEnd = false;
    }
  }, a);

  ns.isEnd = isSolved(ns.cards);
  return ns;
}

}

template<typename State, typename Action>
struct Store {
  State state{};
  std::function<State(const State&, const Action&)> reducer;

  Store(State s, std::function<State(const State&, const Action&)> r) : state(std::move(s)), reducer(std::move(r)) {}
  void dispatch(const Action& a) { state = reducer(state, a); }
};


static void drawOverlay() {
  const int w = GetScreenWidth(), h = GetScreenHeight();
  DrawRectangle(0, 0, w, h, {0,0,0,192});
  const char* txt = "Victory!";
  const int fs = 60;
  DrawText(txt, (w - MeasureText(txt, fs))/2, (h - fs)/2 - 32, fs, WHITE);
  const char* desc = "Click or press R to continue.";
  DrawText(desc, (w - MeasureText(desc, 20))/2, (h + fs)/2, 20, WHITE);
}

static void drawUI(const Puzzle::State& s) {
  ClearBackground(DARKPURPLE);
  for (const auto& card : s.cards) card.draw();
  if (s.isEnd) drawOverlay();
}

/// Keep board centered if window is larger
static void applyBoardTransform() {
  const float boardW = Config::grid * Config::cardSize;
  const float boardH = Config::grid * Config::cardSize;
  const float ox = (GetScreenWidth()  - boardW) * 0.5f;
  const float oy = (GetScreenHeight() - boardH) * 0.5f;
  BeginMode2D(Camera2D{ .offset = { -ox, -oy }, .target = {0,0}, .rotation = 0.f, .zoom = 1.f });
}

static void endBoardTransform() { EndMode2D(); }

int main() {
  SetConfigFlags(FLAG_VSYNC_HINT);
  InitWindow(static_cast<int>(Config::grid * Config::cardSize), static_cast<int>(Config::grid * Config::cardSize), "15 Puzzle");

  // Initial state
  Puzzle::State init{ makeGrid(), false };
  Store<Puzzle::State, Puzzle::Action> store(init, Puzzle::reducer);
  store.dispatch(Puzzle::Restart{}); // start with a solvable shuffle

  while (!WindowShouldClose()) {
    const auto& s = store.state;

    // Input
    if (IsKeyPressed(KEY_S)) store.dispatch(Puzzle::Shuffle{});
    if (IsKeyPressed(KEY_R)) store.dispatch(Puzzle::Restart{});

    if (!s.isEnd) {
      // Mouse
      const Vector2 m = GetMousePosition();
      if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        for (int i = 0; i < static_cast<int>(s.cards.size()); ++i) {
          if (CheckCollisionPointRec(m, s.cards[i].rect)) { store.dispatch(Puzzle::Move{i}); break; }
        }
      }

      // Keyboard arrows
      const auto empty = findEmpty(s.cards);
      if (empty != -1) {
        const int r = static_cast<int>(empty) / Config::grid;
        const int c = static_cast<int>(empty) % Config::grid;
        if (IsKeyPressed(KEY_UP)    && r < Config::grid - 1) store.dispatch(Puzzle::Move{(r+1)*Config::grid + c});
        if (IsKeyPressed(KEY_DOWN)  && r > 0)                store.dispatch(Puzzle::Move{(r-1)*Config::grid + c});
        if (IsKeyPressed(KEY_LEFT)  && c < Config::grid - 1) store.dispatch(Puzzle::Move{r*Config::grid + (c+1)});
        if (IsKeyPressed(KEY_RIGHT) && c > 0)                store.dispatch(Puzzle::Move{r*Config::grid + (c-1)});
      }
    } else {
      if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) store.dispatch(Puzzle::Restart{});
    }

    BeginDrawing();
    applyBoardTransform();
    drawUI(store.state);
    endBoardTransform();
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
