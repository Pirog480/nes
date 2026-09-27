// ============================================================================
//  esp.cpp — the world ESP renderer.
//
//  Geometry (all values come from the menu, see nexus::GetState()):
//      feet  = pawn origin
//      head  = head bone, or origin + (0,0,viewOffsetZ + 8) without a skeleton
//      box   = centred horizontally on feet/head, height = feetY - headY
//              (rejected below 6 px), width = 0.60 * height, 1.5 px thick
//      hpbar = 3 px strip on the left side of the box
//      label = "Name 12m" above the box, with a black shadow
//      bones = pelvis-spine-chest-neck-head, chest-armL/R, pelvis-legL/R
// ============================================================================
#include "esp.h"

#include "nexus_menu.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace nexus_esp {
namespace {

using game::Angles;
using game::Camera;
using game::PlayerSnap;
using game::Snapshot;
using game::Vec3;

constexpr ImVec4 RGB(unsigned char r, unsigned char g, unsigned char b,
                     float a = 1.0f)
{
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

// mirrors the swatches of the Visuals tab
const ImVec4 kAllyColors[3]  = {RGB(0x3b, 0x82, 0xf6), RGB(0x10, 0xb9, 0x81),
                                RGB(0x8b, 0x5c, 0xf6)};
const ImVec4 kEnemyColors[3] = {RGB(0xef, 0x44, 0x44), RGB(0xf5, 0x9e, 0x0b),
                                RGB(0xec, 0x48, 0x99)};

const ImVec4 kHpHigh = RGB(0x10, 0xb9, 0x81); // > 50 %
const ImVec4 kHpMid  = RGB(0xf5, 0x9e, 0x0b); // > 25 %
const ImVec4 kHpLow  = RGB(0xef, 0x44, 0x44);
const ImVec4 kWhite  = RGB(0xff, 0xff, 0xff);
const ImVec4 kBlack  = RGB(0x00, 0x00, 0x00);

// skeleton links, expressed as (slot A, slot B)
const int kBoneLinks[][2] = {
    {game::BonePelvis, game::BoneSpine},
    {game::BoneSpine,  game::BoneChest},
    {game::BoneChest,  game::BoneNeck},
    {game::BoneNeck,   game::BoneHead},
    {game::BoneChest,  game::BoneArmL},
    {game::BoneChest,  game::BoneArmR},
    {game::BonePelvis, game::BoneLegL},
    {game::BonePelvis, game::BoneLegR},
};

constexpr float kBoxThickness   = 1.5f;
constexpr float kBoxWidthRatio  = 0.60f;
constexpr float kMinBoxHeight   = 6.0f;
constexpr float kHpBarWidth     = 3.0f;
constexpr float kHpBarGap       = 2.0f;
constexpr float kLabelGap       = 3.0f;
constexpr float kBoneThickness  = 1.5f;

inline ImU32 Col(const ImVec4& c, float alphaMul = 1.0f)
{
    ImVec4 o   = c;
    o.w       *= alphaMul;
    o.w        = std::clamp(o.w, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(o);
}

inline int ClampIdx(int v, int n) { return std::clamp(v, 0, n - 1); }

const ImVec4& TeamColor(const PlayerSnap& p, const nexus::State& st)
{
    return p.isEnemy ? kEnemyColors[ClampIdx(st.enemyColor, 3)]
                     : kAllyColors[ClampIdx(st.allyColor, 3)];
}

ImVec4 HealthColor(float ratio)
{
    if (ratio > 0.50f)
        return kHpHigh;
    if (ratio > 0.25f)
        return kHpMid;
    return kHpLow;
}

void DrawSkeleton(ImDrawList* dl, const Camera& cam, const PlayerSnap& p,
                  float alpha)
{
    ImVec2 scr[game::BoneCount];
    bool   ok[game::BoneCount] = {};
    for (int i = 0; i < game::BoneCount; ++i) {
        if (!p.boneValid[i])
            continue;
        ok[i] = game::WorldToScreen(cam, p.bone[i], scr[i].x, scr[i].y);
    }

    const ImU32 col = Col(kWhite, 0.9f * alpha);
    for (const auto& link : kBoneLinks) {
        const int a = link[0], b = link[1];
        if (!ok[a] || !ok[b])
            continue;
        dl->AddLine(scr[a], scr[b], col, kBoneThickness);
    }
}

void DrawHealthBar(ImDrawList* dl, float x0, float y0, float y1,
                   const PlayerSnap& p, float alpha)
{
    const float bx0 = x0 - kHpBarGap - kHpBarWidth;
    const float bx1 = x0 - kHpBarGap;
    if (bx1 <= bx0)
        return;

    // track
    dl->AddRectFilled(ImVec2(bx0, y0), ImVec2(bx1, y1), Col(kBlack, 0.55f * alpha));

    const int   hp    = std::max(0, p.health);
    const int   hpMax = std::max(1, p.maxHealth);
    const float ratio = std::clamp((float)hp / (float)hpMax, 0.0f, 1.0f);
    if (ratio <= 0.0f)
        return;

    // filled from the bottom up
    const float top = y1 - (y1 - y0) * ratio;
    dl->AddRectFilled(ImVec2(bx0, top), ImVec2(bx1, y1),
                      Col(HealthColor(ratio), alpha));
}

void DrawLabel(ImDrawList* dl, float cx, float y0, const PlayerSnap& p,
               float distUnits, float alpha)
{
    char buf[96];
    const int meters = (int)std::lround(game::Meters(distUnits));
    if (p.name[0])
        std::snprintf(buf, sizeof(buf), "%s %dm", p.name, meters);
    else
        std::snprintf(buf, sizeof(buf), "%dm", meters);

    const ImVec2 ts  = ImGui::CalcTextSize(buf);
    const float  tx  = cx - ts.x * 0.5f;
    const float  ty  = y0 - kLabelGap - ts.y;

    // black shadow first, then the text itself
    dl->AddText(ImVec2(tx + 1.0f, ty + 1.0f), Col(kBlack, 0.85f * alpha), buf);
    dl->AddText(ImVec2(tx, ty), Col(kWhite, alpha), buf);
}

} // namespace

// ---------------------------------------------------------------------------
void Render(const Snapshot& snap)
{
    const nexus::State& st = nexus::GetState();
    if (!st.esp)
        return;
    if (!snap.valid || !snap.camera.valid || snap.count <= 0)
        return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl)
        return;

    // outlineOpacity is the alpha of the box outline (and, at 0.9x, of the
    // skeleton); the health bar and the label keep their own alpha.
    const float alpha = std::clamp(st.outlineOpacity / 100.0f, 0.0f, 1.0f);

    // 1 m = 39.37 units; drawDistance is in meters
    const float maxUnits = std::max(0.0f, st.drawDistance) * game::kUnitsPerMeter;
    const int   n        = std::min(snap.count, (int)game::kMaxPlayers);

    for (int i = 0; i < n; ++i) {
        const PlayerSnap& p = snap.players[i];
        if (!p.valid || !p.alive || p.isLocal)
            continue;

        const float dist = game::DistanceTo(snap.camera, p.origin);
        if (dist > maxUnits)
            continue;

        float feetX = 0, feetY = 0, headX = 0, headY = 0;
        if (!game::WorldToScreen(snap.camera, p.origin, feetX, feetY))
            continue;
        if (!game::WorldToScreen(snap.camera, game::EspHeadPoint(p), headX, headY))
            continue;

        const float height = feetY - headY;
        if (height < kMinBoxHeight)
            continue;

        const float width = height * kBoxWidthRatio;
        const float cx    = (feetX + headX) * 0.5f;
        const float x0    = cx - width * 0.5f;
        const float x1    = cx + width * 0.5f;
        const float y0    = headY;
        const float y1    = feetY;

        const ImVec4 team = TeamColor(p, st);

        if (st.skeleton)
            DrawSkeleton(dl, snap.camera, p, alpha);

        if (alpha > 0.0f)
            dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), Col(team, alpha), 0.0f,
                        0, kBoxThickness);

        if (st.healthBar)
            DrawHealthBar(dl, x0, y0, y1, p, 1.0f);

        if (st.nameDist)
            DrawLabel(dl, cx, y0, p, dist, 1.0f);
    }
}

} // namespace nexus_esp
