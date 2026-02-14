#include <raylib.h>
#include <vector>
#include <string>
#include <variant>
#include <optional>
#include <algorithm>
#include <random>
#include <ctime>
#include <functional>

// ------------------ Constants ------------------
constexpr float CARD_SIZE = 94.f;
constexpr int GRID = 4;

// ------------------ Card ------------------
struct Card {
    std::string text;
    Rectangle rect;

    Card(std::string t, Rectangle r) : text(t), rect(r) {}

    void draw() const {
        DrawRectangleRec(rect, BLACK);
        Rectangle body = {rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4};
        Color bodyColor = text.empty() ? DARKPURPLE : ORANGE;
        DrawRectangleRec(body, bodyColor);
        if (!text.empty()) {
            int fontSize = 50;
            int x = static_cast<int>(rect.x + (rect.width - MeasureText(text.c_str(), fontSize)) / 2);
            int y = static_cast<int>(rect.y + (rect.height - fontSize) / 2);
            DrawText(text.c_str(), x, y, fontSize, BLACK);
        }
    }
};

// ------------------ Functional Helpers ------------------
auto shuffleTexts = [](std::vector<Card> cards) {
    static std::mt19937 g{std::random_device{}()};
    std::vector<std::string> texts;
    for (auto& c : cards) texts.push_back(c.text);
    std::shuffle(texts.begin(), texts.end(), g);
    for (size_t i = 0; i < cards.size(); ++i) cards[i].text = texts[i];
    return cards;
};

auto findEmpty = [](const std::vector<Card>& cards) {
    auto it = std::find_if(cards.begin(), cards.end(), [](auto& c){ return c.text.empty(); });
    return it != cards.end() ? std::distance(cards.begin(), it) : -1;
};

auto adjacent = [](int i, int j) {
    int r1 = i / GRID, c1 = i % GRID;
    int r2 = j / GRID, c2 = j % GRID;
    return (r1 == r2 && abs(c1 - c2) == 1) || (c1 == c2 && abs(r1 - r2) == 1);
};

auto solved = [](const std::vector<Card>& cards) {
    for (int i = 0; i < 15; ++i)
        if (cards[i].text != std::to_string(i + 1)) return false;
    return cards[15].text.empty();
};

// ------------------ TCA-style ------------------
namespace Puzzle {
    struct State { std::vector<Card> cards; bool isEnd = false; };
    struct Shuffle {}; struct Move { int index; }; struct Restart {};
    using Action = std::variant<Shuffle, Move, Restart>;

    State reducer(const State& s, const Action& a) {
        State ns = s;

        std::visit([&](auto&& act){
            using T = std::decay_t<decltype(act)>;
            if constexpr (std::is_same_v<T, Shuffle>) ns.cards = shuffleTexts(ns.cards);
            else if constexpr (std::is_same_v<T, Move>) {
                int empty = findEmpty(ns.cards);
                if (empty != -1 && adjacent(act.index, empty))
                    std::swap(ns.cards[act.index].text, ns.cards[empty].text);
            }
            else if constexpr (std::is_same_v<T, Restart>) {
                ns.cards = shuffleTexts(ns.cards);
                ns.isEnd = false;
            }
        }, a);

        ns.isEnd = solved(ns.cards);
        return ns;
    }
}

// ------------------ Store ------------------
template<typename State, typename Action>
struct Store {
    State state;
    std::function<State(const State&, const Action&)> reducer;

    Store(State s, decltype(reducer) r) : state(s), reducer(r) {}

    void dispatch(const Action& a) { state = reducer(state, a); }
};

// ------------------ Initial State ------------------
auto initialState = []() {
    Puzzle::State s;
    for (int r = 0; r < GRID; ++r)
        for (int c = 0; c < GRID; ++c) {
            std::string t = (r == GRID-1 && c == GRID-1) ? "" : std::to_string(r*GRID + c + 1);
            s.cards.emplace_back(t, Rectangle{c*CARD_SIZE, r*CARD_SIZE, CARD_SIZE, CARD_SIZE});
        }
    return s;
};

// ------------------ Overlay ------------------
void drawOverlay() {
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, {0,0,0,192});
    const char* txt = "Victory!";
    int fs = 60;
    DrawText(txt, (w - MeasureText(txt, fs))/2, (h - fs)/2 - 32, fs, WHITE);
    const char* desc = "Click to continue.";
    DrawText(desc, (w - MeasureText(desc, 20))/2, (h + fs)/2, 20, WHITE);
}

// ------------------ Main ------------------
int main() {
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(GRID*CARD_SIZE, GRID*CARD_SIZE, "15 Puzzle");

    Store<Puzzle::State, Puzzle::Action> store(initialState(), Puzzle::reducer);
    store.dispatch(Puzzle::Shuffle{});

    while (!WindowShouldClose()) {
        const auto& s = store.state;

        if (IsKeyPressed(KEY_S)) store.dispatch(Puzzle::Shuffle{});

        if (!s.isEnd) {
            Vector2 m = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                for (int i = 0; i < s.cards.size(); ++i)
                    if (CheckCollisionPointRec(m, s.cards[i].rect)) {
                        store.dispatch(Puzzle::Move{i});
                        break;
                    }

            }

            int empty = findEmpty(s.cards);
            int r = empty / GRID, c = empty % GRID;

            if (IsKeyPressed(KEY_UP) && r < GRID-1) store.dispatch(Puzzle::Move{(r+1)*GRID + c});
            if (IsKeyPressed(KEY_DOWN) && r > 0) store.dispatch(Puzzle::Move{(r-1)*GRID + c});
            if (IsKeyPressed(KEY_LEFT) && c < GRID-1) store.dispatch(Puzzle::Move{r*GRID + (c+1)});
            if (IsKeyPressed(KEY_RIGHT) && c > 0) store.dispatch(Puzzle::Move{r*GRID + (c-1)});
        } else if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            store.dispatch(Puzzle::Restart{});
        }

        BeginDrawing();
        ClearBackground(DARKPURPLE);
        for (auto& card : s.cards) card.draw();
        if (s.isEnd) drawOverlay();
        EndDrawing();
    }

    CloseWindow();
}
