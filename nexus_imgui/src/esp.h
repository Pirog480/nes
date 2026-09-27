// ============================================================================
//  esp.h — world ESP (boxes, health bars, names, skeletons) drawn on top of
//  the game with the ImGui background draw list.
//
//  IMPORTANT — call order:
//      nexus::DrawMenu();      // paints the opaque background + FOV circles
//      nexus_esp::Render(snap);
//  DrawMenu() fills the background draw list with an opaque scene, so anything
//  added to that list BEFORE it is covered. Rendering ESP after DrawMenu() puts
//  it above the background/circles but still below the menu window itself
//  (window draw lists are always emitted after the background one).
// ============================================================================
#pragma once

#include "game.h"

namespace nexus_esp {

// Draws every visible player of 'snap' according to nexus::GetState().
// Safe to call with an invalid/empty snapshot (it simply draws nothing).
void Render(const game::Snapshot& snap);

} // namespace nexus_esp
