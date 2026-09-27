// ============================================================================
//  game_offsets.h — Deadlock (internal name "Citadel", Source 2, DX11) offsets.
//
//  Every value below is an ABSOLUTE offset from the start of the object and was
//  taken from the schema dumps shipped with this repository (Client.dll.txt /
//  Server.dll.txt / sdk/, gen/). Nothing here is guessed.
//
//  After a game update only this file (plus kEntityListPatterns in
//  game_windows.cpp and kBoneIndex in game.cpp) normally has to be refreshed —
//  see README.md, "Обновление после патча игры".
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace game {
namespace off {

// ---------------------------------------------------------------------------
// C_BaseEntity
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kGameSceneNode    = 0x308; // CGameSceneNode*
constexpr std::uintptr_t kMaxHealth        = 0x328; // int
constexpr std::uintptr_t kHealth           = 0x32C; // int
constexpr std::uintptr_t kLifeState        = 0x330; // byte, 0 == alive
constexpr std::uintptr_t kTeamNum          = 0x3CF; // byte
constexpr std::uintptr_t kAbsVelocity      = 0x3DC; // Vector

// ---------------------------------------------------------------------------
// CGameSceneNode
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kAbsOrigin        = 0x0D0; // Vector
constexpr std::uintptr_t kDormant          = 0x0EF; // bool

// ---------------------------------------------------------------------------
// CSkeletonInstance (the scene node of a pawn)
//   m_modelState is at 0x170; the engine keeps the pointer to the bone array
//   (matrix3x4_t*, 0x30 bytes each) at modelState + 0x80, i.e. 0x1F0 from the
//   start of CSkeletonInstance.
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kModelState       = 0x170;
constexpr std::uintptr_t kBoneArray        = 0x1F0; // == kModelState + 0x80
constexpr std::uintptr_t kHModel           = 0x210;
constexpr std::size_t    kBoneMatrixSize   = 0x30;  // sizeof(matrix3x4_t)

// ---------------------------------------------------------------------------
// C_BaseModelEntity
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kViewOffset       = 0x790; // Vector (we only need .z)

// ---------------------------------------------------------------------------
// CBasePlayerController / CCitadelPlayerController
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kHPawn            = 0x05E4; // CEntityHandle
constexpr std::uintptr_t kConnected        = 0x0614; // int, 2 == fully in game
constexpr std::uintptr_t kPlayerName       = 0x0618; // char*
constexpr std::uintptr_t kIsLocalController = 0x06A8; // bool
constexpr std::uintptr_t kDesiredFOV       = 0x06AC; // int  (FOV changer)
constexpr std::uintptr_t kHHeroPawn        = 0x0704; // CCitadelPlayerController

// ---------------------------------------------------------------------------
// C_BasePlayerPawn
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kWeaponServices   = 0x0C20; // CPlayer_WeaponServices*
constexpr std::uintptr_t kVAngle           = 0x0CC4; // QAngle (v_angle)

// ---------------------------------------------------------------------------
// CPlayer_Weapon_services
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kHActiveWeapon    = 0x0058; // CEntityHandle

// ---------------------------------------------------------------------------
// C_CitadelPlayerPawn
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kAngEyeAngles     = 0x0E38; // QAngle
constexpr std::uintptr_t kAngClientCamera  = 0x0E50; // pitch,yaw — 2 floats
constexpr std::uintptr_t kAbilityComponent = 0x0EF0; // m_CCitadelAbilityComponent

// ---------------------------------------------------------------------------
// CCitadel_Ability_PrimaryWeapon (the weapon entity itself)
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kNextPrimaryAttack = 0x0C28; // float
constexpr std::uintptr_t kClip             = 0x0C2C;  // int
constexpr std::uintptr_t kInReload         = 0x0C54;  // bool — Active Reload
constexpr std::uintptr_t kReloadAvailableTime = 0x0C5C; // float

// ---------------------------------------------------------------------------
// CEntityHandle
// ---------------------------------------------------------------------------
constexpr std::uint32_t  kHandleIndexMask  = 0x3FFF;
constexpr std::uint32_t  kInvalidHandle    = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// Source 2 entity list traversal (constants, not offsets of a dumped class)
//   chunk = read(list + 0x8 + 0x10 * (index & 0x1FF))
//   entry = chunk + 0x70 * (index >> 9)
//   entity = entry            (layout 0)
//   entity = read(entry+0x10) (layout 1)
// The layout is detected at runtime, see game_windows.cpp.
// ---------------------------------------------------------------------------
constexpr std::uintptr_t kListChunkTable   = 0x8;
constexpr std::uintptr_t kListChunkStride  = 0x10;
constexpr std::uintptr_t kListChunkMask    = 0x1FF;
constexpr std::uintptr_t kListEntryStride  = 0x70;
constexpr std::uintptr_t kListEntityPtr    = 0x10; // layout 1

inline std::uintptr_t HandleIndex(std::uint32_t handle)
{
    return handle & kHandleIndexMask;
}

} // namespace off
} // namespace game
