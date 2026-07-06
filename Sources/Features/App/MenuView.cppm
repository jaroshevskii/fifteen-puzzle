module;

#include <raylib.h>

export module MenuView;

import std;

// A tiny raylib UI kit for the menu-style screens (main menu, pause, game over).
// A centered title over a single column of buttons, with keyboard-selection and
// mouse-hover highlighting and hit-testing. No feature dependencies — the
// feature screens map button indices to their own actions.
export namespace MenuView {

struct Button {
  std::string label;
  bool enabled = true;
};

// Geometry for button `index` of `count`, centered in the window.
Rectangle buttonRect(int index, int count);
// Draws an optional subtitle, the title, and the buttons (highlighting the
// keyboard `selected` index and any moused-over button).
void draw(std::string_view title, std::string_view subtitle, const std::vector<Button> &buttons,
          int selected);
// Index of the enabled button under `point`, or -1.
int buttonAtPoint(const std::vector<Button> &buttons, Vector2 point);
// Moves `selected` with Up/Down arrows, skipping disabled buttons.
void moveSelection(const std::vector<Button> &buttons, int &selected);

} // namespace MenuView

// --- Implementation
// -----------------------------------------------------------

namespace MenuView {

namespace {
constexpr float kButtonWidth = 280.0f;
constexpr float kButtonHeight = 46.0f;
constexpr float kGap = 12.0f;

float columnTop(int count) {
  const float totalHeight = count * kButtonHeight + (count - 1) * kGap;
  return (static_cast<float>(GetScreenHeight()) - totalHeight) / 2.0f + 24.0f;
}
} // namespace

Rectangle buttonRect(int index, int count) {
  const float x = (static_cast<float>(GetScreenWidth()) - kButtonWidth) / 2.0f;
  const float y = columnTop(count) + static_cast<float>(index) * (kButtonHeight + kGap);
  return Rectangle{x, y, kButtonWidth, kButtonHeight};
}

void draw(std::string_view title, std::string_view subtitle, const std::vector<Button> &buttons,
          int selected) {
  const int count = static_cast<int>(buttons.size());
  const float top = columnTop(count);

  const std::string titleText(title);
  constexpr int titleFont = 40;
  DrawText(titleText.c_str(), (GetScreenWidth() - MeasureText(titleText.c_str(), titleFont)) / 2,
           static_cast<int>(top) - 78, titleFont, WHITE);
  if (!subtitle.empty()) {
    const std::string subText(subtitle);
    constexpr int subFont = 18;
    DrawText(subText.c_str(), (GetScreenWidth() - MeasureText(subText.c_str(), subFont)) / 2,
             static_cast<int>(top) - 32, subFont, GRAY);
  }

  const Vector2 mouse = GetMousePosition();
  for (int i = 0; i < count; ++i) {
    const Rectangle rect = buttonRect(i, count);
    const bool hovered = buttons[i].enabled && CheckCollisionPointRec(mouse, rect);
    const bool active = buttons[i].enabled && (i == selected || hovered);
    const Color background = !buttons[i].enabled ? Color{40, 40, 40, 255}
                             : active            ? ORANGE
                                                 : Color{60, 60, 70, 255};
    const Color textColor = !buttons[i].enabled ? Color{110, 110, 110, 255}
                            : active            ? BLACK
                                                : WHITE;
    DrawRectangleRec(rect, background);
    constexpr int font = 22;
    const int textWidth = MeasureText(buttons[i].label.c_str(), font);
    DrawText(buttons[i].label.c_str(), static_cast<int>(rect.x + (rect.width - textWidth) / 2.0f),
             static_cast<int>(rect.y + (rect.height - font) / 2.0f), font, textColor);
  }
}

int buttonAtPoint(const std::vector<Button> &buttons, Vector2 point) {
  const int count = static_cast<int>(buttons.size());
  for (int i = 0; i < count; ++i) {
    if (buttons[i].enabled && CheckCollisionPointRec(point, buttonRect(i, count))) {
      return i;
    }
  }
  return -1;
}

void moveSelection(const std::vector<Button> &buttons, int &selected) {
  const int count = static_cast<int>(buttons.size());
  if (count == 0) {
    return;
  }
  const int step = IsKeyPressed(KEY_DOWN) ? 1 : (IsKeyPressed(KEY_UP) ? -1 : 0);
  if (step == 0) {
    return;
  }
  for (int i = 0; i < count; ++i) {
    selected = (selected + step + count) % count;
    if (buttons[selected].enabled) {
      break;
    }
  }
}

} // namespace MenuView
