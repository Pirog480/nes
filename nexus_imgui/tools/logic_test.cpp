// ============================================================================
//  logic_test.cpp — unit tests for the platform independent half of NEXUS:
//  world->screen projection, angle math, smoothing, the pattern scanner, the
//  bone validator, the aim-chip fallbacks, the keybind parser and the stub
//  backend.
//
//  Prints "ALL PASS" and exits 0 when everything is fine.
// ============================================================================
#include <imgui.h>

#include "../src/aim.h"
#include "../src/esp.h"
#include "../src/game.h"
#include "../src/nexus_menu.h"
#include "../src/pattern.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_checks  = 0;
int g_failures = 0;

void Report(bool ok, const char* what, const std::string& detail)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s  (%s)\n", what, detail.c_str());
    }
}

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

void Check(bool ok, const char* what, const std::string& detail = "")
{
    Report(ok, what, detail);
}

void CheckNear(float a, float b, const char* what, float eps = 1e-3f)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "got %.6f, want %.6f", a, b);
    Report(Near(a, b, eps), what, buf);
}

std::string F(const char* fmt, float v)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), fmt, v);
    return buf;
}

// ---------------------------------------------------------------------------
game::Camera MakeCamera(float px, float py, float pz, float pitch, float yaw,
                        float fov, int w = 1280, int h = 720)
{
    game::Camera c;
    c.valid  = true;
    c.pos    = game::Vec3(px, py, pz);
    c.pitch  = pitch;
    c.yaw    = yaw;
    c.fovX   = fov;
    c.width  = w;
    c.height = h;
    return c;
}

// ---------------------------------------------------------------------------
void TestWorldToScreen()
{
    std::printf("[world->screen]\n");
    const game::Camera cam = MakeCamera(0, 0, 64, 0, 0, 90);

    // straight ahead => dead centre (focal = 640 / tan(45) = 640)
    float sx = 0, sy = 0;
    Check(game::WorldToScreen(cam, game::Vec3(1000, 0, 64), sx, sy),
          "centre point is visible");
    CheckNear(sx, 640.0f, "centre x");
    CheckNear(sy, 360.0f, "centre y");

    // behind the camera => rejected
    Check(!game::WorldToScreen(cam, game::Vec3(-1000, 0, 64), sx, sy),
          "point behind the camera is rejected");
    Check(!game::WorldToScreen(cam, game::Vec3(0.5f, 0, 64), sx, sy),
          "point at z <= 1 is rejected");

    // 45 deg off axis with a 90 deg fov lands exactly on the screen edge
    Check(game::WorldToScreen(cam, game::Vec3(1000, 1000, 64), sx, sy),
          "+45 deg is visible");
    CheckNear(sx, 1280.0f, "+45 deg x == right edge");
    CheckNear(sy, 360.0f, "+45 deg y stays centred");

    Check(game::WorldToScreen(cam, game::Vec3(1000, -1000, 64), sx, sy),
          "-45 deg is visible");
    CheckNear(sx, 0.0f, "-45 deg x == left edge");

    // yaw 90: the old +Y axis now points straight ahead
    const game::Camera cam90 = MakeCamera(0, 0, 64, 0, 90, 90);
    Check(game::WorldToScreen(cam90, game::Vec3(0, 1000, 64), sx, sy),
          "yaw 90 forward is visible");
    CheckNear(sx, 640.0f, "yaw 90 forward x");
    // with yaw 90 the world +X axis is exactly sideways => z_view == 0
    Check(!game::WorldToScreen(cam90, game::Vec3(1000, 0, 64), sx, sy),
          "yaw 90: world +X is perpendicular and rejected");
    Check(game::WorldToScreen(cam90, game::Vec3(500, 1000, 64), sx, sy),
          "yaw 90: a point in the new forward half plane is visible");
    Check(sx < 640.0f, "yaw 90: world +X now bends to the left of the screen",
          F("sx = %.2f", sx));

    // pitch down (+pitch) moves a flat point in front of us upwards on screen
    const game::Camera camP = MakeCamera(0, 0, 64, 15, 0, 90);
    Check(game::WorldToScreen(camP, game::Vec3(600, 0, 64), sx, sy),
          "pitched camera sees the flat point ahead");
    Check(sy < 360.0f, "pitching down pushes the flat point above the centre",
          F("sy = %.2f", sy));

    // the vertical half fov here is atan(360/640) ~= 29.4 deg, so a 30 deg
    // pitch already pushes a flat point off the top of the screen
    const game::Camera camP2 = MakeCamera(0, 0, 64, 30, 0, 90);
    Check(!game::WorldToScreen(camP2, game::Vec3(600, 0, 64), sx, sy),
          "30 deg pitch puts the flat point off screen");
    Check(game::WorldToScreen(camP2, game::Vec3(600, 0, -200), sx, sy),
          "the ground below stays visible while pitching down");
    CheckNear(sx, 640.0f, "the ground below stays centred horizontally");

    // invalid camera / viewport
    game::Camera bad = cam;
    bad.valid = false;
    Check(!game::WorldToScreen(bad, game::Vec3(100, 0, 64), sx, sy),
          "invalid camera is rejected");
    bad = cam;
    bad.width = 0;
    Check(!game::WorldToScreen(bad, game::Vec3(100, 0, 64), sx, sy),
          "zero width is rejected");
}

// ---------------------------------------------------------------------------
void TestCalcAngles()
{
    std::printf("[calc angles]\n");
    const game::Vec3 o(0, 0, 0);

    // target above => negative pitch (Source: pitch > 0 looks DOWN)
    game::Angles a = game::CalcAngles(o, game::Vec3(100, 0, 50));
    CheckNear(a.pitch, -26.565052f, "pitch sign for a target above", 1e-4f);
    CheckNear(a.yaw, 0.0f, "yaw for +X target");

    // target below => positive pitch
    a = game::CalcAngles(o, game::Vec3(100, 0, -50));
    CheckNear(a.pitch, 26.565052f, "pitch sign for a target below", 1e-4f);

    a = game::CalcAngles(o, game::Vec3(0, 100, 0));
    CheckNear(a.yaw, 90.0f, "yaw for +Y target");

    a = game::CalcAngles(o, game::Vec3(100, 100, 0));
    CheckNear(a.yaw, 45.0f, "yaw for a diagonal target");

    a = game::CalcAngles(o, game::Vec3(-100, 0, 0));
    CheckNear(a.yaw, 180.0f, "yaw for -X target");

    a = game::CalcAngles(o, game::Vec3(0, -100, 0));
    CheckNear(a.yaw, -90.0f, "yaw for -Y target");

    // CalcAngles must invert the camera basis: looking with the produced angles
    // puts the target exactly on the screen centre.
    const game::Vec3 tgt(600, -460, 12);
    const game::Vec3 eye(0, 0, 64);
    a = game::CalcAngles(eye, tgt);
    const game::Camera cam = MakeCamera(eye.x, eye.y, eye.z, a.pitch, a.yaw, 90);
    float sx = 0, sy = 0;
    Check(game::WorldToScreen(cam, tgt, sx, sy), "aimed camera sees the target");
    CheckNear(sx, 640.0f, "aimed target x is centred", 0.05f);
    CheckNear(sy, 360.0f, "aimed target y is centred", 0.05f);
}

// ---------------------------------------------------------------------------
void TestNormalize()
{
    std::printf("[normalize / delta]\n");
    CheckNear(game::NormalizeAngle(190.0f), -170.0f, "190 -> -170");
    CheckNear(game::NormalizeAngle(-190.0f), 170.0f, "-190 -> 170");
    CheckNear(game::NormalizeAngle(180.0f), 180.0f, "180 stays 180");
    CheckNear(game::NormalizeAngle(-180.0f), 180.0f, "-180 -> 180");
    CheckNear(game::NormalizeAngle(360.0f), 0.0f, "360 -> 0");
    CheckNear(game::NormalizeAngle(720.0f + 30.0f), 30.0f, "750 -> 30");
    CheckNear(game::NormalizeAngle(-720.0f - 30.0f), -30.0f, "-750 -> -30");

    // wrap across 180
    CheckNear(game::AngleDelta(170.0f, -170.0f), 20.0f, "170 -> -170 is +20");
    CheckNear(game::AngleDelta(-170.0f, 170.0f), -20.0f, "-170 -> 170 is -20");
    CheckNear(game::AngleDelta(0.0f, 90.0f), 90.0f, "0 -> 90 is +90");
    CheckNear(game::AngleDelta(90.0f, 0.0f), -90.0f, "90 -> 0 is -90");
    CheckNear(game::AngleDelta(45.0f, 45.0f), 0.0f, "same angle is 0");
}

// ---------------------------------------------------------------------------
void TestSmooth()
{
    std::printf("[smooth angles]\n");
    const game::Angles cur(10.0f, 20.0f);
    const game::Angles tgt(-30.0f, 100.0f);

    game::Angles r = game::SmoothAngles(cur, tgt, 0.0f);
    CheckNear(r.pitch, cur.pitch, "factor 0 keeps pitch");
    CheckNear(r.yaw, cur.yaw, "factor 0 keeps yaw");

    r = game::SmoothAngles(cur, tgt, 1.0f);
    CheckNear(r.pitch, tgt.pitch, "factor 1 reaches pitch");
    CheckNear(r.yaw, tgt.yaw, "factor 1 reaches yaw");

    r = game::SmoothAngles(cur, tgt, 0.5f);
    CheckNear(r.pitch, -10.0f, "factor 0.5 is the midpoint (pitch)");
    CheckNear(r.yaw, 60.0f, "factor 0.5 is the midpoint (yaw)");

    // halfway across the 180 seam takes the short way
    r = game::SmoothAngles(game::Angles(0, 170), game::Angles(0, -170), 0.5f);
    CheckNear(std::fabs(r.yaw), 180.0f, "wrap: 170 -> -170 halfway is 180");

    r = game::SmoothAngles(game::Angles(0, -170), game::Angles(0, 170), 0.5f);
    CheckNear(std::fabs(r.yaw), 180.0f, "wrap: -170 -> 170 halfway is 180");

    // out-of-range factors are clamped, not extrapolated
    r = game::SmoothAngles(cur, tgt, 5.0f);
    CheckNear(r.pitch, tgt.pitch, "factor > 1 is clamped");
    r = game::SmoothAngles(cur, tgt, -3.0f);
    CheckNear(r.pitch, cur.pitch, "factor < 0 is clamped");

    // the smoothing factor used by the aim: 1 - 0.97 * smooth%
    CheckNear(1.0f - 0.97f * (0.0f / 100.0f), 1.0f, "smooth 0% => instant");
    CheckNear(1.0f - 0.97f * (100.0f / 100.0f), 0.03f, "smooth 100% => slowest");
    CheckNear(1.0f - 0.97f * (50.0f / 100.0f), 0.515f, "smooth 50% => 0.515");
}

// ---------------------------------------------------------------------------
void TestPattern()
{
    std::printf("[pattern scanner]\n");

    //  "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 ?? 48 85 DB"
    unsigned char buf[128];
    std::memset(buf, 0x90, sizeof(buf)); // noise (nop slide)

    const size_t at = 24;
    buf[at + 0] = 0x48; buf[at + 1] = 0x8B; buf[at + 2] = 0x0D;
    buf[at + 3] = 0x78; buf[at + 4] = 0x56; buf[at + 5] = 0x34; buf[at + 6] = 0x12;
    buf[at + 7] = 0x48; buf[at + 8] = 0x8B; buf[at + 9] = 0x01;
    buf[at + 10] = 0xFF; buf[at + 11] = 0x50; buf[at + 12] = 0x18;
    buf[at + 13] = 0x48; buf[at + 14] = 0x85; buf[at + 15] = 0xDB;

    const char* ida =
        "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 ?? 48 85 DB";

    const uintptr_t base = (uintptr_t)buf;
    const uintptr_t hit  = game::pattern::Find(base, sizeof(buf), ida);
    Check(hit == base + at, "Find locates the signature",
          hit ? "found at the wrong offset" : "not found");

    // RIP-relative: addr = match + instrLen + disp32
    const uintptr_t rip =
        game::pattern::FindRip(base, sizeof(buf),
                               "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 ??", 3, 7);
    const uintptr_t want = base + at + 7 + 0x12345678u;
    Check(rip == want, "FindRip resolves rip+disp32",
          rip ? "wrong address" : "not found");

    // a negative displacement must work too
    std::int32_t neg = -100;
    std::memcpy(buf + at + 3, &neg, 4);
    const uintptr_t rip2 = game::pattern::FindRip(base, sizeof(buf),
                                                  "48 8B 0D ?? ?? ?? ??", 3, 7);
    Check(rip2 == base + at + 7 - 100, "FindRip handles a negative disp32");

    // not found / malformed
    Check(game::pattern::Find(base, sizeof(buf), "DE AD BE EF") == 0,
          "missing signature returns 0");
    Check(game::pattern::Find(base, sizeof(buf), "ZZ ZZ") == 0,
          "malformed signature returns 0");
    Check(game::pattern::Find(base, 4, ida) == 0, "range smaller than the pattern returns 0");
    Check(game::pattern::Find((uintptr_t)0, sizeof(buf), ida) == 0, "null base returns 0");

    game::pattern::Signature sig;
    Check(game::pattern::Compile("48 8b 0d ?? ?", sig), "Compile accepts ? and mixed case");
    Check(sig.size() == 5, "Compile token count");
    Check(sig.wild[3] && sig.wild[4] && !sig.wild[0], "Compile wildcard flags");
    Check(!game::pattern::Compile("48 Z", sig), "Compile rejects garbage");
    Check(!game::pattern::Compile("", sig), "Compile rejects an empty string");
    Check(!game::pattern::Compile(nullptr, sig), "Compile rejects nullptr");

    // the three real entity-list candidates must parse
    const char* real[3] = {
        "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 ?? 48 85 DB 74 ?? 4C 8B C3 48 8B CB",
        "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 ??",
        "48 8B 05 ?? ?? ?? ?? 48 8B 08 48 85 C9 74 ?? 48 8B 01"};
    for (int i = 0; i < 3; ++i) {
        game::pattern::Signature s;
        Check(game::pattern::Compile(real[i], s), "entity list candidate parses");
    }
}

// ---------------------------------------------------------------------------
void TestBonesAndChips()
{
    std::printf("[bones / chips]\n");

    // engine bone indices from the Citadel model dump
    const int want[game::BoneCount] = {1, 3, 4, 5, 6, 8, 14, 22, 25};
    for (int i = 0; i < game::BoneCount; ++i)
        Check(game::kBoneIndex[i] == want[i], "kBoneIndex matches the dump");

    game::PlayerSnap p;
    p.origin      = game::Vec3(100, 200, 0);
    p.viewOffsetZ = 64.0f;
    const float z[game::BoneCount] = {36, 46, 54, 62, 68, 54, 54, 18, 18};
    for (int i = 0; i < game::BoneCount; ++i) {
        p.bone[i]     = p.origin + game::Vec3(0, 0, z[i]);
        p.boneValid[i] = true;
    }
    game::ValidateBones(p);
    int alive = 0;
    for (int i = 0; i < game::BoneCount; ++i)
        alive += p.boneValid[i] ? 1 : 0;
    Check(alive == game::BoneCount, "a sane skeleton survives validation");

    // one bone far away => only that bone is dropped
    p.bone[game::BoneArmR] = p.origin + game::Vec3(0, 0, 200);
    game::ValidateBones(p);
    Check(!p.boneValid[game::BoneArmR], "a bone 200 units away is rejected");
    Check(p.boneValid[game::BoneHead], "the rest stays valid");

    // head below the pelvis => the whole set is dropped
    p.bone[game::BoneArmR] = p.origin + game::Vec3(0, 0, 54);
    p.bone[game::BoneHead] = p.origin + game::Vec3(0, 0, 10);
    game::ValidateBones(p);
    alive = 0;
    for (int i = 0; i < game::BoneCount; ++i)
        alive += p.boneValid[i] ? 1 : 0;
    Check(alive == 0, "head below pelvis drops every bone");

    // fewer than five valid bones => dropped as well
    p = game::PlayerSnap{};
    p.origin = game::Vec3(0, 0, 0);
    p.viewOffsetZ = 64.0f;
    for (int i = 0; i < 4; ++i) {
        p.bone[i]      = game::Vec3(0, 0, 30.0f + i);
        p.boneValid[i] = true;
    }
    game::ValidateBones(p);
    alive = 0;
    for (int i = 0; i < game::BoneCount; ++i)
        alive += p.boneValid[i] ? 1 : 0;
    Check(alive == 0, "fewer than 5 bones drops the set");

    // NaN bones are rejected
    p = game::PlayerSnap{};
    p.origin = game::Vec3(0, 0, 0);
    p.viewOffsetZ = 64.0f;
    for (int i = 0; i < game::BoneCount; ++i) {
        p.bone[i]      = game::Vec3(0, 0, 20.0f + i);
        p.boneValid[i] = true;
    }
    p.bone[game::BoneHead].x = std::nanf("");
    game::ValidateBones(p);
    Check(!p.boneValid[game::BoneHead], "a non-finite bone is rejected");

    // chip fallbacks (no skeleton at all)
    p = game::PlayerSnap{};
    p.origin      = game::Vec3(10, 20, 30);
    p.viewOffsetZ = 64.0f;
    for (int i = 0; i < game::BoneCount; ++i)
        p.boneValid[i] = false;

    game::Vec3 v = game::ChipPoint(p, game::ChipHead);
    CheckNear(v.z, 30 + 64 + 6, "fallback head z");
    v = game::ChipPoint(p, game::ChipNeck);
    CheckNear(v.z, 30 + 64 - 6, "fallback neck z");
    v = game::ChipPoint(p, game::ChipChest);
    CheckNear(v.z, 30 + 0.55f * 64, "fallback chest z");
    v = game::ChipPoint(p, game::ChipArms);
    CheckNear(v.z, 30 + 0.55f * 64, "fallback arms -> chest");
    v = game::ChipPoint(p, game::ChipPelvis);
    CheckNear(v.z, 30 + 0.35f * 64, "fallback pelvis z");
    v = game::ChipPoint(p, game::ChipLegs);
    CheckNear(v.z, 30 + 8, "fallback legs z");
    v = game::EspHeadPoint(p);
    CheckNear(v.z, 30 + 64 + 8, "ESP fallback head z");
    CheckNear(v.x, 10, "fallback keeps x");

    // a real bone wins over the fallback
    p.boneValid[game::BoneHead] = true;
    p.bone[game::BoneHead]      = game::Vec3(10, 20, 30 + 70);
    v = game::ChipPoint(p, game::ChipHead);
    CheckNear(v.z, 100.0f, "a valid head bone is used");
    v = game::EspHeadPoint(p);
    CheckNear(v.z, 100.0f, "ESP uses the head bone too");

    // an unusable view offset falls back to 64
    p = game::PlayerSnap{};
    p.viewOffsetZ = 0.0f;
    v = game::ChipPoint(p, game::ChipHead);
    CheckNear(v.z, 64.0f + 6.0f, "viewOffset 0 falls back to 64");
}

// ---------------------------------------------------------------------------
void TestKeyNames()
{
    std::printf("[keybind parsing]\n");
    struct { const char* name; ImGuiKey key; } cases[] = {
        {"MB1", ImGuiKey_MouseLeft},
        {"MB2", ImGuiKey_MouseRight},
        {"MB3", ImGuiKey_MouseMiddle},
        {"MB4", ImGuiKey_MouseX1},
        {"MB5", ImGuiKey_MouseX2},
        {"MOUSE1", ImGuiKey_MouseLeft},
        {"MOUSE4", ImGuiKey_MouseX1},
        {"R", ImGuiKey_R},
        {"r", ImGuiKey_R},
        {"5", ImGuiKey_5},
        {"Z", ImGuiKey_Z},
        {"Left Shift", ImGuiKey_LeftShift},
        {"LEFTSHIFT", ImGuiKey_LeftShift},
        {"Right Shift", ImGuiKey_RightShift},
        {"Left Ctrl", ImGuiKey_LeftCtrl},
        {"Right Ctrl", ImGuiKey_RightCtrl},
        {"Left Alt", ImGuiKey_LeftAlt},
        {"Right Alt", ImGuiKey_RightAlt},
        {"Space", ImGuiKey_Space},
        {"Tab", ImGuiKey_Tab},
        {"Enter", ImGuiKey_Enter},
        {"Return", ImGuiKey_Enter},
        {"Insert", ImGuiKey_Insert},
        {"Delete", ImGuiKey_Delete},
        {"Home", ImGuiKey_Home},
        {"End", ImGuiKey_End},
        {"Page Up", ImGuiKey_PageUp},
        {"Page Down", ImGuiKey_PageDown},
        {"Caps Lock", ImGuiKey_CapsLock},
        {"F1", ImGuiKey_F1},
        {"F12", ImGuiKey_F12},
        {"Mouse Left", ImGuiKey_MouseLeft},
        // unknown => ImGuiKey_None (the aim stays off, fail-safe)
        {"", ImGuiKey_None},
        {nullptr, ImGuiKey_None},
        {"\xe2\x80\x93", ImGuiKey_None}, // the "no key" en-dash of the menu
        {"Bogus", ImGuiKey_None},
        {"KEY9999", ImGuiKey_None},
    };

    for (const auto& c : cases) {
        const ImGuiKey got = nexus_aim::KeyFromName(c.name);
        char what[128];
        std::snprintf(what, sizeof(what), "KeyFromName(\"%s\")",
                      c.name ? c.name : "(null)");
        char detail[128];
        std::snprintf(detail, sizeof(detail), "got %d, want %d", (int)got,
                      (int)c.key);
        Check(got == c.key, what, detail);
    }

    // ImGuiKey_A..Z and 0..9 really are contiguous
    Check((int)ImGuiKey_Z - (int)ImGuiKey_A == 25, "ImGuiKey_A..Z is contiguous");
    Check((int)ImGuiKey_9 - (int)ImGuiKey_0 == 9, "ImGuiKey_0..9 is contiguous");
}

// ---------------------------------------------------------------------------
void TestStubBackend()
{
    std::printf("[stub backend]\n");
    game::Snapshot snap;
    snap.valid = true;
    snap.count = 5;
    snap.players[0].valid = true;
    snap.weaponInReload   = true;

    Check(!game::Capture(snap), "stub Capture() reports no game");
    Check(!snap.valid, "stub Capture() clears the snapshot");
    Check(snap.count == 0, "stub Capture() resets the player count");
    Check(!snap.players[0].valid, "stub Capture() resets the players");
    Check(!snap.weaponInReload, "stub Capture() resets the weapon flag");
    Check(!snap.camera.valid, "stub Capture() resets the camera");

    // must be callable without a game present
    game::SetCameraAngles(1.0f, 2.0f);
    game::TickMisc(snap);
    Check(game::BackendName() != nullptr && game::BackendName()[0] != '\0',
          "BackendName() is not empty");

    // ESP/aim on an empty snapshot must not crash
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime   = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();
    nexus_esp::Render(snap);
    nexus_aim::Tick(snap);
    nexus::DrawMenu();
    nexus_esp::Render(snap); // ESP after the menu, like in the DLL
    ImGui::EndFrame();
    ImGui::Render();
    ImGui::DestroyContext();
    Check(true, "ESP/aim survive an empty snapshot");

    CheckNear(game::Meters(game::kUnitsPerMeter), 1.0f, "1 m == 39.37 units");
}

// ---------------------------------------------------------------------------
void TestSnapshotHelpers()
{
    std::printf("[snapshot]\n");
    game::Snapshot s;
    s.valid = true;
    s.count = 3;
    s.players[1].name[0] = 'x';
    s.Clear();
    Check(!s.valid && s.count == 0 && s.players[1].name[0] == '\0',
          "Snapshot::Clear resets everything");
    Check(!s.camera.valid, "Clear resets the camera");

    // the enemy rule used by the capture: team != local && both >= 2
    auto isEnemy = [](int team, int local) {
        return team != local && team >= 2 && local >= 2;
    };
    Check(isEnemy(3, 2) && isEnemy(2, 3), "2 vs 3 are enemies");
    Check(!isEnemy(2, 2), "same team is not an enemy");
    Check(!isEnemy(1, 2), "team 1 (neutral) is not an enemy");
    Check(!isEnemy(3, 0), "no local team => no enemies");

    CheckNear(game::AngleBetween(game::Vec3(1, 0, 0), game::Vec3(0, 1, 0)), 90.0f,
              "AngleBetween 90");
    CheckNear(game::AngleBetween(game::Vec3(1, 0, 0), game::Vec3(1, 0, 0)), 0.0f,
              "AngleBetween 0");
    CheckNear(game::AngleBetween(game::Vec3(1, 0, 0), game::Vec3(-1, 0, 0)), 180.0f,
              "AngleBetween 180");
    CheckNear(game::AngleBetween(game::Vec3(0, 0, 0), game::Vec3(1, 0, 0)), 180.0f,
              "AngleBetween with a zero vector");

    game::Vec3 f = game::CameraForward(0, 0);
    CheckNear(f.x, 1.0f, "forward at yaw 0 is +X");
    CheckNear(f.z, 0.0f, "forward at pitch 0 is flat");
    f = game::CameraForward(90, 0);
    CheckNear(f.z, -1.0f, "pitch 90 looks straight down");
    game::Vec3 r = game::CameraRight(0);
    CheckNear(r.x, 0.0f, "right at yaw 0: x");
    CheckNear(r.y, 1.0f, "right at yaw 0 is (-sin y, cos y, 0)");
    game::Vec3 u = game::CameraUp(0, 0);
    CheckNear(u.z, 1.0f, "up at pitch 0 is +Z");

    // the basis is orthonormal for an arbitrary orientation
    const float pitch = 23.0f, yaw = -142.0f;
    f = game::CameraForward(pitch, yaw);
    r = game::CameraRight(yaw);
    u = game::CameraUp(pitch, yaw);
    CheckNear(game::Dot(f, r), 0.0f, "forward _|_ right", 1e-4f);
    CheckNear(game::Dot(f, u), 0.0f, "forward _|_ up", 1e-4f);
    CheckNear(game::Dot(r, u), 0.0f, "right _|_ up", 1e-4f);
    CheckNear(game::Length(f), 1.0f, "forward is unit", 1e-4f);
    CheckNear(game::Length(u), 1.0f, "up is unit", 1e-4f);
}

} // namespace

int main()
{
    std::printf("nexus_logic_test — ImGui %s\n", IMGUI_VERSION);

    TestWorldToScreen();
    TestCalcAngles();
    TestNormalize();
    TestSmooth();
    TestPattern();
    TestBonesAndChips();
    TestKeyNames();
    TestSnapshotHelpers();
    TestStubBackend();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::printf("FAILED\n");
    return 1;
}
