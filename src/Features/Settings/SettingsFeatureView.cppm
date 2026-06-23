module;

#include <raylib.h>

export module SettingsFeatureView;

import std;
import SettingsFeature;
import AppSettings;

// The settings screen: rows for sound, auto-resume, board size, resolution,
// fullscreen, and player name — driven by keyboard (Up/Down to move,
// Enter/Left/Right to change, typing for the name) and mouse. Esc/back is
// handled by AppFeatureView (→ Dismiss).
export namespace SettingsFeatureView {

std::vector<SettingsFeature::Action> collectActions(const SettingsFeature::State &state);
void draw(const SettingsFeature::State &state);

} // namespace SettingsFeatureView

// --- Implementation
// -----------------------------------------------------------

namespace SettingsFeatureView {

namespace {
enum Row { kSound, kAutoResume, kBoardSize, kResolution, kFullscreen, kPlayer, kRowCount };
constexpr int kMinGrid = 4;
constexpr int kMaxGrid = 13;
int selected = 0;

Rectangle rowRect(int index) {
  const float w = 400.0f;
  const float h = 42.0f;
  const float gap = 10.0f;
  const float x = (static_cast<float>(GetScreenWidth()) - w) / 2.0f;
  const float top = 92.0f;
  return Rectangle{x, top + static_cast<float>(index) * (h + gap), w, h};
}

// Standard resolutions the monitor can show (filtered to <= its current size),
// plus the monitor's native size — the closest raylib lets us get to "the modes
// the system supports". Largest-first so cycling feels natural.
std::vector<std::pair<int, int>> availableResolutions() {
  const int monitor = GetCurrentMonitor();
  const int mw = GetMonitorWidth(monitor);
  const int mh = GetMonitorHeight(monitor);
  static constexpr std::pair<int, int> presets[] = {{800, 600},   {1024, 768}, {1280, 720},
                                                    {1366, 768},  {1600, 900}, {1920, 1080},
                                                    {2560, 1440}, {3840, 2160}};
  std::vector<std::pair<int, int>> out;
  for (const auto &[w, h] : presets) {
    if ((mw <= 0 || w <= mw) && (mh <= 0 || h <= mh)) {
      out.emplace_back(w, h);
    }
  }
  if (mw > 0 && mh > 0 && std::ranges::find(out, std::pair{mw, mh}) == out.end()) {
    out.emplace_back(mw, mh);
  }
  if (out.empty()) {
    out.emplace_back(mw > 0 ? mw : 800, mh > 0 ? mh : 600);
  }
  std::ranges::sort(out);
  return out;
}

int resolutionIndex(const std::vector<std::pair<int, int>> &list, int w, int h) {
  const auto it = std::ranges::find(list, std::pair{w, h});
  return it == list.end() ? 0 : static_cast<int>(std::ranges::distance(list.begin(), it));
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
  const bool prev = IsKeyPressed(KEY_LEFT);
  const bool next = IsKeyPressed(KEY_RIGHT);

  switch (selected) {
  case kSound:
    if (activate || clicked == kSound) {
      actions.push_back(SettingsFeature::SoundToggled{});
    }
    break;
  case kAutoResume:
    if (activate || clicked == kAutoResume) {
      actions.push_back(SettingsFeature::AutoResumeToggled{});
    }
    break;
  case kBoardSize: {
    int grid = settings.lastBoardSize;
    if (next || clicked == kBoardSize) {
      grid = std::min(kMaxGrid, grid + 1);
    } else if (prev) {
      grid = std::max(kMinGrid, grid - 1);
    }
    if (grid != settings.lastBoardSize) {
      actions.push_back(SettingsFeature::BoardSizeSelected{grid});
    }
    break;
  }
  case kResolution: {
    const auto list = availableResolutions();
    int index = resolutionIndex(list, settings.displayWidth, settings.displayHeight);
    if (next || clicked == kResolution) {
      index = (index + 1) % static_cast<int>(list.size());
    } else if (prev) {
      index = (index + static_cast<int>(list.size()) - 1) % static_cast<int>(list.size());
    }
    const auto [w, h] = list[index];
    if (w != settings.displayWidth || h != settings.displayHeight) {
      actions.push_back(SettingsFeature::ResolutionSelected{w, h});
    }
    break;
  }
  case kFullscreen:
    if (activate || clicked == kFullscreen) {
      actions.push_back(SettingsFeature::FullscreenToggled{});
    }
    break;
  case kPlayer: {
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
  DrawText("Settings", 24, 28, 36, WHITE);

  const std::array<std::string, kRowCount> rows{
      std::format("Sound          {}", settings.isSoundEnabled ? "On" : "Off"),
      std::format("Auto-resume    {}", settings.autoResume ? "On" : "Off"),
      std::format("Board size     {}x{}  (< >)", settings.lastBoardSize, settings.lastBoardSize),
      std::format("Resolution     {}x{}  (< >)", settings.displayWidth, settings.displayHeight),
      std::format("Fullscreen     {}", settings.fullscreen ? "On" : "Off"),
      std::format("Player         {}{}", settings.playerName, selected == kPlayer ? "_" : "")};

  for (int i = 0; i < kRowCount; ++i) {
    const Rectangle rect = rowRect(i);
    const bool active = i == selected;
    DrawRectangleRec(rect, active ? Color{60, 60, 70, 255} : Color{32, 32, 40, 255});
    DrawText(rows[static_cast<std::size_t>(i)].c_str(), static_cast<int>(rect.x) + 14,
             static_cast<int>(rect.y + (rect.height - 20) / 2.0f), 20, active ? ORANGE : WHITE);
  }

  DrawText("Esc — back", 24, GetScreenHeight() - 32, 18, GRAY);
}

} // namespace SettingsFeatureView
