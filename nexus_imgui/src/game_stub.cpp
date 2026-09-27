// ============================================================================
//  game_stub.cpp — the "no game here" backend.
//
//  Linked into every non-Windows / in-process-less target (nexus_headless,
//  nexus_logic_test, nexus_demo). Capture() always reports "nothing to show",
//  so ESP and the aim silently do nothing instead of touching foreign memory.
//  The real implementation lives in dll/game_windows.cpp.
// ============================================================================
#include "game.h"

namespace game {

bool Capture(Snapshot& out)
{
    out.Clear();
    return false;
}

void SetCameraAngles(float /*pitch*/, float /*yaw*/) {}

void TickMisc(const Snapshot& /*snap*/) {}

void SetViewport(int /*width*/, int /*height*/) {}

const char* BackendName() { return "stub"; }

} // namespace game
