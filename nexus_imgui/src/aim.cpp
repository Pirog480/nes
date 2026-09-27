// ============================================================================
//  aim.cpp — vector aim assist (Normal mode of the Aim tab).
//
//  Pipeline (see README):
//    1. every living enemy contributes the first accepted chip of aimPoints[]
//       (Head, Neck, Chest, Arms, Pelvis, Legs — real bone, else the
//       view-offset derived point),
//    2. a chip is accepted when the angle to it is <= normal.fov / 2
//       (the slider holds the FULL cone),
//    3. "HS Limit" shrinks the allowed cone for the head chip only,
//    4. "Predict Movement" offsets the point by velocity * (dist / bullet speed),
//    5. the winner is picked by targetPrio (Closest / Health / Damage),
//    6. the result is smoothed and handed to game::SetCameraAngles().
//
//  PSilent is deliberately not implemented — this file never reads
//  nexus::State::psilent.
// ============================================================================
#include "aim.h"

#include "nexus_menu.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace nexus_aim {
namespace {

using game::Angles;
using game::Camera;
using game::PlayerSnap;
using game::Snapshot;
using game::Vec3;

// ---------------------------------------------------------------------------
// tuning constants
// ---------------------------------------------------------------------------
constexpr float kBulletSpeed = 1100.0f; // units / second
constexpr float kSmoothScale = 0.97f;   // factor = 1 - kSmoothScale * smooth%
constexpr float kMaxSmooth   = 1.0f - kSmoothScale; // slowest possible factor

// ---------------------------------------------------------------------------
// keybind -> ImGuiKey
// ---------------------------------------------------------------------------
std::string CanonicalKey(const char* s)
{
    std::string out;
    if (!s)
        return out;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        if (*p == ' ' || *p == '_' || *p == '-')
            continue;
        if (*p < 0x20 || *p > 0x7E)
            return {}; // non-ASCII (e.g. the "no key" en-dash) => unknown
        out.push_back((char)std::toupper((int)*p));
    }
    return out;
}

struct KeyAlias {
    const char* name;
    ImGuiKey    key;
};

// mouse buttons + a few common spellings; everything else falls through to the
// generic ImGui::GetKeyName() comparison below.
const KeyAlias kAliases[] = {
    {"MB1", ImGuiKey_MouseLeft},    {"MOUSE1", ImGuiKey_MouseLeft},
    {"M1", ImGuiKey_MouseLeft},     {"MB2", ImGuiKey_MouseRight},
    {"MOUSE2", ImGuiKey_MouseRight},{"M2", ImGuiKey_MouseRight},
    {"MB3", ImGuiKey_MouseMiddle},  {"MOUSE3", ImGuiKey_MouseMiddle},
    {"M3", ImGuiKey_MouseMiddle},   {"MB4", ImGuiKey_MouseX1},
    {"MOUSE4", ImGuiKey_MouseX1},   {"M4", ImGuiKey_MouseX1},
    {"MB5", ImGuiKey_MouseX2},      {"MOUSE5", ImGuiKey_MouseX2},
    {"M5", ImGuiKey_MouseX2},
    {"RETURN", ImGuiKey_Enter},     {"ENTER", ImGuiKey_Enter},
    {"ESC", ImGuiKey_Escape},       {"ESCAPE", ImGuiKey_Escape},
    {"CTRL", ImGuiKey_LeftCtrl},    {"CONTROL", ImGuiKey_LeftCtrl},
    {"SHIFT", ImGuiKey_LeftShift},  {"ALT", ImGuiKey_LeftAlt},
    {"SPACE", ImGuiKey_Space},      {"TAB", ImGuiKey_Tab},
    {"CAPSLOCK", ImGuiKey_CapsLock},{"DEL", ImGuiKey_Delete},
    {"INS", ImGuiKey_Insert},       {"PGUP", ImGuiKey_PageUp},
    {"PGDN", ImGuiKey_PageDown},
};

ImGuiKey ResolveKey(const std::string& canon)
{
    if (canon.empty())
        return ImGuiKey_None;

    for (const KeyAlias& a : kAliases)
        if (canon == a.name)
            return a.key;

    // a single letter / digit
    if (canon.size() == 1) {
        const char c = canon[0];
        if (c >= 'A' && c <= 'Z')
            return (ImGuiKey)(ImGuiKey_A + (c - 'A'));
        if (c >= '0' && c <= '9')
            return (ImGuiKey)(ImGuiKey_0 + (c - '0'));
    }

    // everything ImGui itself knows: "LeftShift", "PageUp", "F5", "MouseX1"...
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const char* nm = ImGui::GetKeyName((ImGuiKey)k);
        if (!nm || !nm[0])
            continue;
        if (CanonicalKey(nm) == canon)
            return (ImGuiKey)k;
    }
    return ImGuiKey_None;
}

// ---------------------------------------------------------------------------
// target bookkeeping
// ---------------------------------------------------------------------------
struct Candidate {
    bool   ok    = false;
    Vec3   pos;
    float  angle = 180.0f; // degrees from the current view direction
    float  dist  = 0.0f;   // units
    int    hp    = 0;
    int    chip  = -1;
};

// "Damage" priority: low HP and close range first.
inline float DamageScore(const Candidate& c)
{
    return (float)std::max(1, c.hp) * (1.0f + c.dist / 1000.0f);
}

// Lower score == better target.
inline float PriorityScore(int prio, const Candidate& c)
{
    switch (prio) {
    case 1:  return (float)std::max(0, c.hp); // Health
    case 2:  return DamageScore(c);           // Damage
    default: return c.angle;                  // Closest
    }
}

} // namespace

// ---------------------------------------------------------------------------
ImGuiKey KeyFromName(const char* name)
{
    // tiny cache: the name changes only when the user re-binds the key
    static std::string s_lastName;
    static ImGuiKey    s_lastKey = ImGuiKey_None;
    static bool        s_have    = false;

    const std::string n = name ? name : "";
    if (s_have && n == s_lastName)
        return s_lastKey;

    s_lastName = n;
    s_lastKey  = ResolveKey(CanonicalKey(n.c_str()));
    s_have     = true;
    return s_lastKey;
}

// ---------------------------------------------------------------------------
void Tick(const Snapshot& snap)
{
    const nexus::State& st = nexus::GetState();

    // ---- gates -------------------------------------------------------------
    if (st.aimType != 0)        return; // Normal only — PSilent is untouched
    if (!st.aimAssist)          return;
    if (!st.normal.enable)      return;
    if (st.menuOpen)            return;
    if (!snap.valid || !snap.camera.valid || snap.count <= 0)
        return;

    const ImGuiKey key = KeyFromName(st.activationKey.c_str());
    if (key == ImGuiKey_None)   return; // unknown binding => aim stays off
    bool keyDown = ImGui::IsKeyDown(key);
#ifdef _WIN32
    // Some games use Raw Input and do not deliver ordinary button transitions
    // consistently to the hooked WndProc. Query the physical mouse state too,
    // so an MB1 activation binding works while the game has cursor capture.
    if (key == ImGuiKey_MouseLeft)
        keyDown = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    else if (key == ImGuiKey_MouseRight)
        keyDown = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    else if (key == ImGuiKey_MouseMiddle)
        keyDown = (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    else if (key == ImGuiKey_MouseX1)
        keyDown = (::GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;
    else if (key == ImGuiKey_MouseX2)
        keyDown = (::GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
#endif
    if (!keyDown) return;

    const Camera& cam = snap.camera;
    const Vec3    fwd = game::CameraForward(cam.pitch, cam.yaw);

    // the slider holds the FULL cone in degrees
    const float cone = std::clamp(st.normal.fov, 0.0f, 180.0f) * 0.5f;
    if (cone <= 0.0f)
        return;
    const float hsCone =
        cone * std::clamp(st.hsLimitVal, 0.0f, 100.0f) / 100.0f;

    // ---- 1..4: best chip of every living enemy -----------------------------
    Candidate best;
    float     bestScore = 0.0f;

    const int n = std::min(snap.count, (int)game::kMaxPlayers);
    for (int i = 0; i < n; ++i) {
        const PlayerSnap& p = snap.players[i];
        if (!p.valid || !p.alive || p.isLocal || !p.isEnemy)
            continue;

        Candidate c;
        for (int chip = 0; chip < game::ChipCount; ++chip) {
            if (!st.aimPoints[chip])
                continue;

            Vec3  pos  = game::ChipPoint(p, chip);
            float dist = game::Distance(cam.pos, pos);

            if (st.predictMovement && dist > 0.0f) {
                pos   = pos + p.velocity * (dist / kBulletSpeed);
                dist  = game::Distance(cam.pos, pos);
            }

            const float ang = game::AngleBetween(fwd, pos - cam.pos);
            if (ang > cone)
                continue; // outside of the aim cone
            if (st.hsLimit && chip == game::ChipHead && ang > hsCone)
                continue; // head only inside the inner N% of the cone

            c.ok    = true;
            c.pos   = pos;
            c.angle = ang;
            c.dist  = dist;
            c.hp    = p.health;
            c.chip  = chip;
            break; // first accepted chip wins (aimPoints order = priority)
        }

        if (!c.ok)
            continue;

        const float score = PriorityScore(st.targetPrio, c);
        if (!best.ok || score < bestScore) {
            best      = c;
            bestScore = score;
        }
    }

    if (!best.ok)
        return;

    // ---- 6..7: smooth and write -------------------------------------------
    const float smooth = std::clamp(st.normal.smooth, 0.0f, 100.0f);
    const float factor = std::max(0.0f, 1.0f - kSmoothScale * (smooth / 100.0f));
    (void)kMaxSmooth;

    const Angles want = game::CalcAngles(cam.pos, best.pos);
    const Angles out  = game::SmoothAngles(Angles(cam.pitch, cam.yaw), want,
                                          factor);

    game::SetCameraAngles(out.pitch, out.yaw);
}

} // namespace nexus_aim
