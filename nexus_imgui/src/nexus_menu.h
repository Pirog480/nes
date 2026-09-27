// ============================================================================
//  nexus_menu.h — the NEXUS cheat-menu UI, ported from uusa.html to Dear ImGui.
//
//  Usage (inside an existing ImGui frame, between NewFrame and Render):
//
//      nexus::DrawMenu();
//
//  The module owns its state; every setting from the HTML page has a mirror
//  field in nexus::State (see GetState()).
// ============================================================================
#pragma once

#include <imgui.h>

#include <string>

namespace nexus {

// ---------------------------------------------------------------------------
// All settings from the original page (defaults match uusa.html)
// ---------------------------------------------------------------------------
struct State {
    // ---- window / navigation -------------------------------------------------
    bool menuOpen     = true;
    bool minimized    = false;
    int  activeTab    = 0; // 0 aim, 1 visuals, 2 heroes, 3 misc, 4 settings

    // ---- aim tab -------------------------------------------------------------
    bool aimAssist       = true;
    bool predictMovement = true;
    bool ignoreObstacles = false;

    int aimType = 0; // 0 normal, 1 psilent

    struct NormalAim {
        bool  enable    = true;
        float fov       = 90.0f;   // 0..180
        float smooth    = 50.0f;   // 0..100 %
        int   colorIdx  = 0;
        float opacity   = 80.0f;   // 0..100 %
        bool  fovShown  = false;
    } normal;

    struct PSilentAim {
        bool  enable    = false;
        float fov       = 45.0f;   // 0..180
        float hitChance = 75.0f;   // 0..100 %
        int   colorIdx  = 0;
        float opacity   = 80.0f;
        bool  fovShown  = false;
    } psilent;

    int  targetPrio = 0; // 0 closest, 1 health, 2 damage
    bool aimPoints[6] = {true, false, false, false, false, false};

    std::string activationKey = "MB4";

    bool  hsLimit    = false;
    float hsLimitVal = 40.0f;

    // ---- visuals tab ---------------------------------------------------------
    bool esp         = true;
    bool skeleton    = true;
    bool healthBar   = false;
    bool nameDist    = true;
    bool chams       = false;

    float outlineOpacity = 100.0f; // 0..100 %
    float drawDistance   = 250.0f; // 0..500 m
    int   allyColor      = 0;
    int   enemyColor     = 0;

    // ---- heroes tab ----------------------------------------------------------
    char heroSearch[64] = {};
    int  heroOpen       = -1;      // index of the hero with the panel open
    bool abilityOn[4]   = {true, true, false, true};
    float abilitySmooth[4] = {50, 50, 50, 50};

    // ---- misc tab ------------------------------------------------------------
    bool  bunnyHop           = true;
    bool  autoAccept         = false;
    bool  autoActiveReload   = false;
    bool  fovChanger         = false;
    float fovValue           = 90.0f; // 70..130

    // ---- settings tab --------------------------------------------------------
    int   theme          = 0;      // 0 dark, 1 light
    std::string menuKey  = "INSERT";
    ImGuiKey     menuKeyKey = ImGuiKey_Insert;
    bool   uiAnimations  = true;
    float  uiScale       = 100.0f; // 70..130 %
    float  menuOpacity   = 100.0f; // 30..100 %
    int    accentIdx     = 0;
};

// Access the live state (also used by tools/tests to preset scenarios).
State& GetState();

// Reset settings — mirrors window.resetSettings() from the HTML.
void ResetSettings();

// Toast notification — mirrors window.showToast(msg).
void ShowToast(const char* msg);

// Toggle menu visibility (Insert key handler lives inside DrawMenu).
void ToggleMenu();

// Background clear color for the current theme (draw your GL clear with it).
ImVec4 GetClearColor();

// Main entry: call once per frame while the ImGui context is current.
void DrawMenu();

} // namespace nexus
