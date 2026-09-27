// ============================================================================
//  game.h — Deadlock game-state snapshot + the pure (platform independent)
//  math used by ESP and the vector aim.
//
//  Split in two halves on purpose:
//    * everything declared here except Capture()/SetCameraAngles()/TickMisc()
//      is pure math and data, compiled into the static library `nexus_logic`
//      and unit-tested by tools/logic_test.cpp;
//    * Capture()/SetCameraAngles()/TickMisc() are the *backend*: the real
//      implementation lives in dll/game_windows.cpp (in-process, Windows),
//      headless builds link src/game_stub.cpp instead, which reports
//      "no game" and makes every feature degrade gracefully.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace game {

// ---------------------------------------------------------------------------
// basic types
// ---------------------------------------------------------------------------
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
};

// Source angles: pitch > 0 looks DOWN, yaw 0 points along +X. Roll is unused.
struct Angles {
    float pitch = 0.0f, yaw = 0.0f;

    Angles() = default;
    Angles(float P, float Y) : pitch(P), yaw(Y) {}
};

Vec3  operator+(const Vec3& a, const Vec3& b);
Vec3  operator-(const Vec3& a, const Vec3& b);
Vec3  operator*(const Vec3& a, float k);
float Dot(const Vec3& a, const Vec3& b);
float Length(const Vec3& a);
float Distance(const Vec3& a, const Vec3& b);
Vec3  Normalized(const Vec3& a);
bool  IsFinite(const Vec3& a);

// ---------------------------------------------------------------------------
// bones
// ---------------------------------------------------------------------------
// Slots of PlayerSnap::bone[] — the engine bone indices are kBoneIndex[].
enum BoneSlot {
    BonePelvis = 0,
    BoneSpine  = 1,
    BoneChest  = 2,
    BoneNeck   = 3,
    BoneHead   = 4,
    BoneArmL   = 5,
    BoneArmR   = 6,
    BoneLegL   = 7,
    BoneLegR   = 8,
    BoneCount  = 9
};

// Engine (skeleton) indices, validated against the Citadel model dump.
extern const int kBoneIndex[BoneCount];
extern const char* const kBoneName[BoneCount];

// ---------------------------------------------------------------------------
// aim chips — order matches nexus::State::aimPoints[6]
// ---------------------------------------------------------------------------
enum AimChip {
    ChipHead = 0,
    ChipNeck = 1,
    ChipChest = 2,
    ChipArms = 3,
    ChipPelvis = 4,
    ChipLegs = 5,
    ChipCount = 6
};
extern const char* const kChipName[ChipCount];

// ---------------------------------------------------------------------------
// constants
// ---------------------------------------------------------------------------
constexpr float kUnitsPerMeter = 39.37f; // 1 m = 39.37 Source units
constexpr int   kMaxPlayers    = 64;
constexpr float kDefaultViewOffsetZ = 64.0f;

// ---------------------------------------------------------------------------
// snapshot
// ---------------------------------------------------------------------------
struct Camera {
    bool  valid = false;
    Vec3  pos;               // pawn origin + (0,0,viewOffsetZ)
    float pitch = 0.0f;      // from m_angClientCamera
    float yaw   = 0.0f;
    float fovX  = 90.0f;     // from m_iDesiredFOV, clamped to 60..140
    int   width  = 0;        // backbuffer size in pixels
    int   height = 0;
};

struct PlayerSnap {
    bool  valid    = false;
    bool  alive    = false;
    bool  isLocal  = false;
    bool  isEnemy  = false;
    int   team     = 0;
    int   health   = 0;
    int   maxHealth = 0;
    char  name[64] = {};

    Vec3  origin;
    Vec3  velocity;
    float viewOffsetZ = 0.0f;

    bool  boneValid[BoneCount] = {};
    Vec3  bone[BoneCount];
};

struct Snapshot {
    bool       valid = false;   // false => no game / not in a match
    int        count = 0;       // number of used entries in players[]
    PlayerSnap players[kMaxPlayers];
    Camera     camera;
    bool       weaponInReload = false; // m_bInReload of the active weapon

    void Clear();
};

// ---------------------------------------------------------------------------
// camera basis (Source conventions: yaw 0 = +X, pitch > 0 = down)
// ---------------------------------------------------------------------------
Vec3 CameraForward(float pitchDeg, float yawDeg);
Vec3 CameraRight(float yawDeg);   // (-sin yaw, cos yaw, 0) — screen X axis
Vec3 CameraUp(float pitchDeg, float yawDeg);

// World -> screen. Returns false when the point is behind the camera or off
// screen. focal = (W/2)/tan(fovX/2); sx = W/2 + x*focal/z; sy = H/2 - y*focal/z.
bool WorldToScreen(const Camera& cam, const Vec3& world, float& sx, float& sy);

// Angles that make the camera look from 'from' towards 'to'.
Angles CalcAngles(const Vec3& from, const Vec3& to);

// Angle helpers (degrees).
float NormalizeAngle(float a);           // -> (-180, 180]
float AngleDelta(float from, float to);  // signed shortest arc from->to
float AngleBetween(const Vec3& a, const Vec3& b); // unsigned, degrees
Angles SmoothAngles(const Angles& current, const Angles& target, float factor);

// Bone sanity: per bone |b - origin| < 96 and finite; the whole set is dropped
// when fewer than 5 bones survive or when the head sits below the pelvis.
// After a rejection every boneValid[] is false, so ESP/aim automatically fall
// back to the view-offset derived points.
void ValidateBones(PlayerSnap& p);

// World position of an aim chip: the real bone when it is valid, otherwise the
// computed fallback derived from the view offset (head +6, neck -6, chest
// 0.55*, pelvis 0.35*, arms -> chest, legs -> +8 units).
Vec3 ChipPoint(const PlayerSnap& p, int chip);

// "Head" used by the ESP box (bone, or origin + (0,0,viewOffsetZ + 8)).
Vec3 EspHeadPoint(const PlayerSnap& p);

// Distance helpers.
float DistanceTo(const Camera& cam, const Vec3& world); // units
inline float Meters(float units) { return units / kUnitsPerMeter; }

// ---------------------------------------------------------------------------
// backend (dll/game_windows.cpp or src/game_stub.cpp)
// ---------------------------------------------------------------------------

// Fills 'out' from the live game. Returns false when there is nothing to draw
// (module/pattern not found, not in a match, local player missing, ...).
bool Capture(Snapshot& out);

// Writes the camera angles in-process: m_angClientCamera, v_angle and
// m_angEyeAngles of the local pawn.
void SetCameraAngles(float pitch, float yaw);

// Per-frame misc features that need the OS (FOV changer, Auto Active Reload).
void TickMisc(const Snapshot& snap);

// Tells the backend the current backbuffer size; Capture() uses it for the
// camera viewport. Ignored by the stub.
void SetViewport(int width, int height);

// Short backend description for the log/UI ("stub", "deadlock", ...).
const char* BackendName();

} // namespace game
