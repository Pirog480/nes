// ============================================================================
//  aim.h — vector ("normal") aim assist.
//
//  Only the Normal mode of the Aim tab is implemented (aimType == 0).
//  PSilent is intentionally NOT touched — it stays a pure UI setting.
//
//  Call once per frame, before nexus::DrawMenu():
//      game::Snapshot snap;
//      if (game::Capture(snap)) nexus_aim::Tick(snap);
// ============================================================================
#pragma once

#include <imgui.h>

#include "game.h"

namespace nexus_aim {

// Maps the textual keybind stored in the menu ("MB4", "Left Shift", "F5", "R")
// onto an ImGuiKey. Unknown names map to ImGuiKey_None, which keeps the aim
// switched off (fail-safe).
ImGuiKey KeyFromName(const char* name);

// Runs one aim step for the given snapshot (writes the camera angles through
// game::SetCameraAngles when a target is acquired).
void Tick(const game::Snapshot& snap);

} // namespace nexus_aim
