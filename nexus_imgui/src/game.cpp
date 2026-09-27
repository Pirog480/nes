// ============================================================================
//  game.cpp — pure math / snapshot helpers (no OS, no game, no ImGui).
//  Everything here is covered by tools/logic_test.cpp.
// ============================================================================
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game {
namespace {

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kRad2D = 180.0f / kPi;
constexpr float kDeg2R = kPi / 180.0f;

inline float Deg(float r) { return r * kRad2D; }
inline float Rad(float d) { return d * kDeg2R; }

} // namespace

// ---------------------------------------------------------------------------
// tables
// ---------------------------------------------------------------------------
// Engine bone indices of the Citadel player skeleton (pelvis .. legs).
const int kBoneIndex[BoneCount] = {
    1,  // BonePelvis
    3,  // BoneSpine
    4,  // BoneChest
    5,  // BoneNeck
    6,  // BoneHead
    8,  // BoneArmL
    14, // BoneArmR
    22, // BoneLegL
    25, // BoneLegR
};

const char* const kBoneName[BoneCount] = {
    "pelvis", "spine", "chest", "neck", "head",
    "armL",   "armR",  "legL",  "legR"};

const char* const kChipName[ChipCount] = {
    "Head", "Neck", "Chest", "Arms", "Pelvis", "Legs"};

// ---------------------------------------------------------------------------
// Vec3
// ---------------------------------------------------------------------------
Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }

float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }

float Distance(const Vec3& a, const Vec3& b) { return Length(a - b); }

Vec3 Normalized(const Vec3& a)
{
    const float l = Length(a);
    if (l <= 1e-6f)
        return {0.0f, 0.0f, 0.0f};
    return a * (1.0f / l);
}

bool IsFinite(const Vec3& a)
{
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------
void Snapshot::Clear()
{
    valid           = false;
    count           = 0;
    weaponInReload  = false;
    camera          = Camera{};
    for (int i = 0; i < kMaxPlayers; ++i)
        players[i] = PlayerSnap{};
}

// ---------------------------------------------------------------------------
// camera basis
// ---------------------------------------------------------------------------
Vec3 CameraForward(float pitchDeg, float yawDeg)
{
    const float cp = std::cos(Rad(pitchDeg));
    const float sp = std::sin(Rad(pitchDeg));
    const float cy = std::cos(Rad(yawDeg));
    const float sy = std::sin(Rad(yawDeg));
    // pitch > 0 looks down => forward.z is negative (Source: forward.z = -sp)
    return {cp * cy, cp * sy, -sp};
}

Vec3 CameraRight(float yawDeg)
{
    // screen X axis, per the Source-2 convention used by this project
    return {-std::sin(Rad(yawDeg)), std::cos(Rad(yawDeg)), 0.0f};
}

Vec3 CameraUp(float pitchDeg, float yawDeg)
{
    const float sp = std::sin(Rad(pitchDeg));
    const float cp = std::cos(Rad(pitchDeg));
    return {sp * std::cos(Rad(yawDeg)), sp * std::sin(Rad(yawDeg)), cp};
}

// ---------------------------------------------------------------------------
// projection
// ---------------------------------------------------------------------------
bool WorldToScreen(const Camera& cam, const Vec3& world, float& sx, float& sy)
{
    sx = sy = 0.0f;
    if (!cam.valid || cam.width <= 0 || cam.height <= 0)
        return false;

    const Vec3  d = world - cam.pos;
    const float z = Dot(d, CameraForward(cam.pitch, cam.yaw));
    if (!(z > 1.0f)) // behind the camera (or degenerate)
        return false;

    const float fov = std::clamp(cam.fovX, 1.0f, 179.0f);
    const float halfW = cam.width * 0.5f;
    const float halfH = cam.height * 0.5f;
    const float focal = halfW / std::tan(Rad(fov) * 0.5f);

    const float x = Dot(d, CameraRight(cam.yaw));
    const float y = Dot(d, CameraUp(cam.pitch, cam.yaw));

    const float px = halfW + x * focal / z;
    const float py = halfH - y * focal / z;

    if (!std::isfinite(px) || !std::isfinite(py))
        return false;
    // reject points that land outside of the screen
    if (px < 0.0f || py < 0.0f || px > (float)cam.width || py > (float)cam.height)
        return false;

    sx = px;
    sy = py;
    return true;
}

Angles CalcAngles(const Vec3& from, const Vec3& to)
{
    const Vec3  d    = to - from;
    const float hyp  = std::sqrt(d.x * d.x + d.y * d.y);
    Angles      out;
    out.pitch = -Deg(std::atan2(d.z, hyp));
    out.yaw   = Deg(std::atan2(d.y, d.x));
    return out;
}

float NormalizeAngle(float a)
{
    if (!std::isfinite(a))
        return 0.0f;
    a = std::fmod(a, 360.0f); // -> (-360, 360)
    if (a > 180.0f)
        a -= 360.0f;
    else if (a <= -180.0f)
        a += 360.0f;
    return a; // (-180, 180]
}

float AngleDelta(float from, float to) { return NormalizeAngle(to - from); }

float AngleBetween(const Vec3& a, const Vec3& b)
{
    const float la = Length(a), lb = Length(b);
    if (la <= 1e-6f || lb <= 1e-6f)
        return 180.0f;
    const float c = std::clamp(Dot(a, b) / (la * lb), -1.0f, 1.0f);
    return Deg(std::acos(c));
}

Angles SmoothAngles(const Angles& current, const Angles& target, float factor)
{
    const float f = std::clamp(std::isfinite(factor) ? factor : 0.0f, 0.0f, 1.0f);
    Angles      out;
    out.pitch = NormalizeAngle(current.pitch +
                               AngleDelta(current.pitch, target.pitch) * f);
    out.yaw   = NormalizeAngle(current.yaw +
                             AngleDelta(current.yaw, target.yaw) * f);
    return out;
}

// ---------------------------------------------------------------------------
// bones / chips
// ---------------------------------------------------------------------------
void ValidateBones(PlayerSnap& p)
{
    int survivors = 0;
    for (int i = 0; i < BoneCount; ++i) {
        const bool ok = p.boneValid[i] && IsFinite(p.bone[i]) &&
                        Distance(p.bone[i], p.origin) < 96.0f;
        p.boneValid[i] = ok;
        if (ok)
            ++survivors;
    }

    // head below the pelvis means the skeleton is garbage (ragdoll / stale
    // buffer / wrong model state) — drop everything.
    if (p.boneValid[BoneHead] && p.boneValid[BonePelvis] &&
        p.bone[BoneHead].z < p.bone[BonePelvis].z)
        survivors = 0;

    if (survivors < 5) {
        for (int i = 0; i < BoneCount; ++i)
            p.boneValid[i] = false;
    }
}

namespace {

inline float SafeViewOffset(const PlayerSnap& p)
{
    return (std::isfinite(p.viewOffsetZ) && p.viewOffsetZ > 1.0f &&
            p.viewOffsetZ < 200.0f)
               ? p.viewOffsetZ
               : kDefaultViewOffsetZ;
}

} // namespace

Vec3 ChipPoint(const PlayerSnap& p, int chip)
{
    const float vo = SafeViewOffset(p);

    switch (chip) {
    case ChipHead:
        if (p.boneValid[BoneHead])
            return p.bone[BoneHead];
        return p.origin + Vec3(0, 0, vo + 6.0f);
    case ChipNeck:
        if (p.boneValid[BoneNeck])
            return p.bone[BoneNeck];
        return p.origin + Vec3(0, 0, vo - 6.0f);
    case ChipChest:
        if (p.boneValid[BoneChest])
            return p.bone[BoneChest];
        return p.origin + Vec3(0, 0, 0.55f * vo);
    case ChipArms:
        if (p.boneValid[BoneArmL])
            return p.bone[BoneArmL];
        if (p.boneValid[BoneArmR])
            return p.bone[BoneArmR];
        return p.origin + Vec3(0, 0, 0.55f * vo); // arms -> chest
    case ChipPelvis:
        if (p.boneValid[BonePelvis])
            return p.bone[BonePelvis];
        return p.origin + Vec3(0, 0, 0.35f * vo);
    case ChipLegs:
        if (p.boneValid[BoneLegL])
            return p.bone[BoneLegL];
        if (p.boneValid[BoneLegR])
            return p.bone[BoneLegR];
        return p.origin + Vec3(0, 0, 8.0f);
    default:
        return p.origin;
    }
}

Vec3 EspHeadPoint(const PlayerSnap& p)
{
    if (p.boneValid[BoneHead])
        return p.bone[BoneHead];
    return p.origin + Vec3(0, 0, SafeViewOffset(p) + 8.0f);
}

float DistanceTo(const Camera& cam, const Vec3& world)
{
    return Distance(cam.pos, world);
}

} // namespace game
