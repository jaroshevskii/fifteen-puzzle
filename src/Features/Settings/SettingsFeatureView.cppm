module;

#include <raylib.h>

export module SettingsFeatureView;

import std;
import SettingsFeature;
import AppSettings;

// The settings screen: four rows (sound, auto-resume, board size, player name)
// driven by keyboard (Up/Down to move, Enter/Left/Right to change, typing for
// the name) and mouse. Esc/back is handled by AppFeatureView (→ Dismiss).
export namespace SettingsFeatureView {

std::vector<SettingsFeature::Action> collectActions(const SettingsFeature::State &state);
void draw(const SettingsFeature::State &state);

} // namespace SettingsFeatureView

// --- Implementation
// -----------------------------------------------------------

namespace SettingsFeatureView {

namespace {
constexpr int kRowCount = 4; // sound, auto-resume, board size, player name
constexpr int kMinGrid = 4;
constexpr int kMaxGrid = 13;
int selected = 0;

Rectangle rowRect(int index) {
  const float w = 360.0f;
  const float h = 44.0f;
  const float gap = 10.0f;
  const float x = (static_cast<float>(GetScreenWidth()) - w) / 2.0f;
  const float top = 96.0f;
  return Rectangle{x, top + static_cast<float>(index) * (h + gap), w, h};
}
} // namespace

std::vector<SettingsFeature::Action> collectActions(const SettingsFeature::State &state) {
  std::vector<SettingsFeature::Action> actions;
  const AppSettings::Settings &settings = state.settings.get();

  if (IsKeyPressed(KEY_DOWN)) {
    selected = (selected + 1) % kRowCount;
  }
  if (IsKeyPressed(KEY_UP)) {
    selected = (selected + kRowCount - 1) % kRowCount;
  }

  // Mouse: clicking a row selects it (and activates toggles / board-size).
  int clicked = -1;
  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
    const Vector2 mouse = GetMousePosition();
    for (int i = 0; i < kRowCount; ++i) {
      if (CheckCollisionPointRec(mouse, rowRect(i))) {
        selected = i;
        clicked = i;
        break;
      }
    }
  }

  const bool activate = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);

  switch (selected) {
  case 0: // sound
    if (activate || clicked == 0) {
      actions.push_back(SettingsFeature::SoundToggled{});
    }
    break;
  case 1: // auto-resume
    if (activate || clicked == 1) {
      actions.push_back(SettingsFeature::AutoResumeToggled{});
    }
    break;
  case 2: { // board size
    int grid = settings.lastBoardSize;
    if (IsKeyPressed(KEY_RIGHT) || clicked == 2) {
      grid = std::min(kMaxGrid, grid + 1);
    } else if (IsKeyPressed(KEY_LEFT)) {
      grid = std::max(kMinGrid, grid - 1);
    }
    if (grid != settings.lastBoardSize) {
      actions.push_back(SettingsFeature::BoardSizeSelected{grid});
    }
    break;
  }
  case 3: { // player name
    std::string name = settings.playerName;
    for (int c = GetCharPressed(); c > 0; c = GetCharPressed()) {
      if (c >= 32 && c < 127 && name.size() < 16) {
        name.push_back(static_cast<char>(c));
      }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !name.empty()) {
      name.pop_back();
    }
    if (name != settings.playerName) {
      actions.push_back(SettingsFeature::PlayerNameChanged{std::move(name)});
    }
    break;
  }
  default:
    break;
  }

  return actions;
}

void draw(const SettingsFeature::State &state) {
  const AppSettings::Settings &settings = state.settings.get();
  DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 235});
  DrawText("Settings", 24, 32, 36, WHITE);

  const std::array<std::string, kRowCount> rows{
      std::format("Sound          {}", settings.isSoundEnabled ? "On" : "Off"),
      std::format("Auto-resume    {}", settings.autoResume ? "On" : "Off"),
      std::format("Board size     {}x{}  (< >)", settings.lastBoardSize, settings.lastBoardSize),
      std::format("Player         {}{}", settings.playerName, selected == 3 ? "_" : "")};

  for (int i = 0; i < kRowCount; ++i) {
    const Rectangle rect = rowRect(i);
    const bool active = i == selected;
    DrawRectangleRec(rect, active ? Color{60, 60, 70, 255} : Color{32, 32, 40, 255});
    DrawText(rows[i].c_str(), static_cast<int>(rect.x) + 14,
             static_cast<int>(rect.y + (rect.height - 20) / 2.0f), 20, active ? ORANGE : WHITE);
  }

  DrawText("Esc — back", 24, GetScreenHeight() - 32, 18, GRAY);
}

} // namespace SettingsFeatureView
