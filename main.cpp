
// Full TCA Redux-like 15-Puzzle in C++ with Raylib (C API)

#include <raylib.h>
#include <vector>
#include <string>
#include <variant>
#include <optional>
#include <algorithm>
#include <random>
#include <ctime>
#include <cstdlib>
#include <functional>

// Constants
constexpr float card_size = 94.f;
constexpr int cards_per_row = 4;
constexpr int cards_per_column = 4;
constexpr int ident = 16;

// Card struct
struct Card {
    std::string text;
    Rectangle rectangle;

    Card(std::string t, Rectangle r) : text(t), rectangle(r) {}

    void draw() const {
        DrawRectangleRec(rectangle, BLACK);
        Rectangle body = {rectangle.x + 2, rectangle.y + 2, rectangle.width - 4, rectangle.height - 4};
        Color body_color = text.empty() ? DARKPURPLE : ORANGE;
        DrawRectangleRec(body, body_color);
        if (!text.empty()) {
            int font_size = 50;
            int pos_y = static_cast<int>(rectangle.y + (rectangle.height - font_size) / 2);
            int pos_x = static_cast<int>(rectangle.x + (rectangle.width - MeasureText(text.c_str(), font_size)) / 2);
            DrawText(text.c_str(), pos_x, pos_y, font_size, BLACK);
        }
    }
};

// Helper functions
bool check_consecutive(const std::vector<Card>& cards) {
    for (size_t i = 0; i < cards.size() - 2; ++i) {
        const std::string& a = cards[i].text;
        const std::string& b = cards[i + 1].text;
        const std::string& c = cards[i + 2].text;
        if (!a.empty() && !b.empty() && !c.empty()) {
            try {
                int ia = std::stoi(a), ib = std::stoi(b), ic = std::stoi(c);
                if (ia + 1 == ib && ib + 1 == ic) return true;
            } catch (...) {}
        }
    }
    return false;
}

std::random_device rd;
std::mt19937 g(rd()); // random engine

void shuffle_card_text(std::vector<Card>& cards) {
    std::vector<std::string> texts;
    for (const auto& c : cards) texts.push_back(c.text);
    std::shuffle(texts.begin(), texts.end(), g); // <-- use shuffle instead
    for (size_t i = 0; i < cards.size(); ++i) cards[i].text = texts[i];
    if (check_consecutive(cards)) std::reverse(cards.begin(), cards.end());
}

size_t get_empty_card_index(const std::vector<Card>& cards) {
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].text.empty()) return i;
    }
    return static_cast<size_t>(-1);
}

bool is_adjacent(size_t i, size_t empty) {
    int row_i = static_cast<int>(i / cards_per_row);
    int col_i = static_cast<int>(i % cards_per_row);
    int row_e = static_cast<int>(empty / cards_per_row);
    int col_e = static_cast<int>(empty % cards_per_row);
    return (row_i == row_e && std::abs(col_i - col_e) == 1) ||
           (col_i == col_e && std::abs(row_i - row_e) == 1);
}

bool is_sorted(const std::vector<Card>& cards) {
    std::vector<std::string> expected;
    for (int j = 1; j < 16; ++j) expected.push_back(std::to_string(j));
    std::vector<std::string> actual;
    for (size_t k = 0; k < 15; ++k) actual.push_back(cards[k].text);
    return actual == expected && cards.back().text.empty();
}

// TCA-like structure
namespace PuzzleFeature {
    struct State {
        std::vector<Card> cards;
        bool is_end_game = false;
    };

    struct Shuffle {};
    struct Move { size_t index; };
    struct Restart {};

    using Action = std::variant<Shuffle, Move, Restart>;

    using ReducerResult = std::pair<State, std::optional<Action>>;

    ReducerResult reducer(const State& state, const Action& action) {
        State newState = state;

        if (std::holds_alternative<Shuffle>(action)) {
            shuffle_card_text(newState.cards);
        } else if (auto* move = std::get_if<Move>(&action)) {
            size_t empty = get_empty_card_index(newState.cards);
            if (empty != static_cast<size_t>(-1) && is_adjacent(move->index, empty)) {
                std::swap(newState.cards[move->index].text, newState.cards[empty].text);
            }
            newState.is_end_game = is_sorted(newState.cards);
        } else if (std::holds_alternative<Restart>(action)) {
            newState.is_end_game = false;
            return reducer(newState, Shuffle{});
        }

        return {newState, std::nullopt};
    }
}

// Generic Store
template<typename State, typename Action>
class Store {
    State state_;
    std::function<std::pair<State, std::optional<Action>>(const State&, const Action&)> reducer_;
public:
    Store(const State& initial, auto reducer) : state_(initial), reducer_(reducer) {}

    void dispatch(const Action& action) {
        auto [newState, effect] = reducer_(state_, action);
        state_ = newState;
        if (effect) dispatch(*effect);
    }

    const State& state() const { return state_; }
};

// Create initial state
PuzzleFeature::State create_initial_state() {
    PuzzleFeature::State s;
    for (int row = 0; row < cards_per_column; ++row) {
        for (int col = 0; col < cards_per_row; ++col) {
            float x = static_cast<float>(col) * card_size;
            float y = static_cast<float>(row) * card_size;
            std::string text = (row == 3 && col == 3) ? "" : std::to_string(row * cards_per_row + col + 1);
            Rectangle rect = {x, y, card_size, card_size};
            s.cards.emplace_back(text, rect);
        }
    }
    return s;
}

// Draw overlay
void draw_overlay() {
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, {0, 0, 0, 192});
    const char* text = "Victory!";
    int font_size = 60;
    int pos_x = (w - MeasureText(text, font_size)) / 2;
    int pos_y = (h - font_size) / 2 - ident * 2;
    DrawText(text, pos_x, pos_y, font_size, WHITE);
    const char* desc = "Click to continue.";
    int desc_x = (w - MeasureText(desc, 20)) / 2;
    int desc_y = pos_y + font_size + ident;
    DrawText(desc, desc_x, desc_y, 20, WHITE);
}

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));  // For random

    auto initial = create_initial_state();
    Store<PuzzleFeature::State, PuzzleFeature::Action> store(initial, PuzzleFeature::reducer);

    store.dispatch(PuzzleFeature::Shuffle{});

    int screen_size = static_cast<int>(card_size * 4);
    
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(screen_size, screen_size, "15 Puzzle");

    while (!WindowShouldClose()) {
        const auto& current_state = store.state();

        if (IsKeyPressed(KEY_S)) {
            store.dispatch(PuzzleFeature::Shuffle{});
        }

        if (!current_state.is_end_game) {
            Vector2 mouse = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                for (size_t i = 0; i < current_state.cards.size(); ++i) {
                    if (CheckCollisionPointRec(mouse, current_state.cards[i].rectangle)) {
                        size_t empty = get_empty_card_index(current_state.cards);
                        if (is_adjacent(i, empty)) {
                            store.dispatch(PuzzleFeature::Move{i});
                            break;
                        }
                    }
                }
            }

            // Keyboard arrow support
            size_t empty = get_empty_card_index(current_state.cards);
            int row = static_cast<int>(empty / cards_per_row);
            int col = static_cast<int>(empty % cards_per_row);

            if (IsKeyPressed(KEY_UP) && row < cards_per_column - 1) { // Move tile below up
                size_t idx = (row + 1) * cards_per_row + col;
                store.dispatch(PuzzleFeature::Move{idx});
            }
            if (IsKeyPressed(KEY_DOWN) && row > 0) { // Move tile above down
                size_t idx = (row - 1) * cards_per_row + col;
                store.dispatch(PuzzleFeature::Move{idx});
            }
            if (IsKeyPressed(KEY_LEFT) && col < cards_per_row - 1) { // Move tile right to left
                size_t idx = row * cards_per_row + (col + 1);
                store.dispatch(PuzzleFeature::Move{idx});
            }
            if (IsKeyPressed(KEY_RIGHT) && col > 0) { // Move tile left to right
                size_t idx = row * cards_per_row + (col - 1);
                store.dispatch(PuzzleFeature::Move{idx});
            }
        } else {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                store.dispatch(PuzzleFeature::Restart{});
            }
        }

        BeginDrawing();
        ClearBackground(DARKPURPLE);

        for (const auto& card : current_state.cards) {
            card.draw();
        }

        if (current_state.is_end_game) {
            draw_overlay();
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
