// ============================================================================
//  game_windows.cpp — the real (in-process) Deadlock backend for game.h.
//
//  Everything here reads the game's own memory through the offsets from
//  game_offsets.h; nothing is written except the camera angles (aim), the
//  desired FOV (FOV changer) and a synthetic 'R' key press (Auto Active
//  Reload). There is deliberately NO anti-cheat bypass of any kind.
//
//  When the module / pattern / entity list cannot be resolved every feature
//  degrades gracefully: Capture() reports false and the UI keeps working.
//  Diagnostics go to %TEMP%\nexus_dll.log (see dll_log.h).
// ============================================================================
#include "../src/game.h"
#include "../src/game_offsets.h"
#include "../src/nexus_menu.h"
#include "../src/pattern.h"
#include "dll_log.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace game {
namespace {

// ---------------------------------------------------------------------------
// entity-list RIP patterns (three known candidates; the first that resolves
// to a readable pointer wins). rel = 3 (disp32), len = 7 (whole instruction).
// ---------------------------------------------------------------------------
const char* const kEntityListPatterns[3] = {
    "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 ?? 48 85 DB 74 ?? 4C 8B C3 48 8B CB",
    "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 ??",
    "48 8B 05 ?? ?? ?? ?? 48 8B 08 48 85 C9 74 ?? 48 8B 01",
};
// Andromeda's old, engine-owned accessor path. Prefer GetBaseEntity when both
// the entity-system singleton and function resolve; keep the raw-list walker as
// a fallback for builds where these signatures have changed.
const char* const kGameEntitySystemPattern =
    "48 8B 0D ?? ?? ?? ?? 8B D0 E8 ?? ?? ?? ?? 44 8B 83 C0 00 00 00";
const char* const kGetBaseEntityPattern =
    "4C 8D 49 ? 81 FA ? ? ? ? 77";

constexpr int kPatternRel  = 3;
constexpr int kPatternLen  = 7;
constexpr int kScanMax     = 2048; // entity indices covered by the rolling scan
constexpr int kScanBatch   = 128;  // bounded discovery work per frame
constexpr int kLayoutScan  = 512;  // indices used for the layout autodetect
constexpr ULONGLONG kReloadIntervalMs = 90;

// ---------------------------------------------------------------------------
// cached state
// ---------------------------------------------------------------------------
uintptr_t g_clientBase = 0;
size_t    g_clientSize = 0;
uintptr_t g_entityList = 0; // the live list pointer (not its storage)
uintptr_t g_entitySystemSlot = 0;
bool      g_entitySystemSlotSearched = false;
uintptr_t g_entitySystem = 0;
uintptr_t g_getBaseEntityFn = 0;
bool      g_getBaseEntitySearched = false;
bool      g_entityApiMode = false;
bool      g_entityApiFallbackLogged = false;
bool      g_layoutFailLogged = false;
int       g_layout     = -1;
bool      g_patternFailLogged = false;
int       g_scanCursor = 0;
bool      g_controllerCached[kScanMax] = {};
std::vector<int> g_controllerIndices;
std::vector<uintptr_t> g_activeControllers;

uintptr_t g_localController = 0;
uintptr_t g_localPawn       = 0;
int       g_localTeam       = -1;

int       g_vpW = 0, g_vpH = 0;
ULONGLONG g_lastReloadMs = 0;
ULONGLONG g_lastCaptureDiagMs = 0;
ULONGLONG g_lastLayoutProbeMs = 0;

void LogCaptureStatus(const char* status, int count = 0, int alive = 0,
                      int enemies = 0, int width = 0, int height = 0)
{
    const ULONGLONG now = ::GetTickCount64();
    if (now - g_lastCaptureDiagMs < 2000)
        return;
    g_lastCaptureDiagMs = now;
    NEXUS_LOG("game: capture %s, controllers=%zu, local=%p, pawn=%p, team=%d, players=%d alive=%d enemies=%d viewport=%dx%d",
              status, g_controllerIndices.size(), (void*)g_localController,
              (void*)g_localPawn, g_localTeam, count, alive, enemies, width,
              height);
}

// ---------------------------------------------------------------------------
// guarded memory access (no SEH, just page-state checks)
// ---------------------------------------------------------------------------
bool Readable(const void* p, size_t n)
{
    if (!p || n == 0)
        return false;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!::VirtualQuery(p, &mbi, sizeof(mbi)))
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    const DWORD prot = mbi.Protect & 0xFF;
    if (prot == PAGE_NOACCESS || prot == PAGE_GUARD || prot == 0)
        return false;
    const uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    if ((uintptr_t)p + n > regionEnd) {
        // crossing a region border: re-check the tail
        MEMORY_BASIC_INFORMATION mbi2 = {};
        if (!::VirtualQuery((const char*)p + n - 1, &mbi2, sizeof(mbi2)))
            return false;
        if (mbi2.State != MEM_COMMIT)
            return false;
        const DWORD p2 = mbi2.Protect & 0xFF;
        if (p2 == PAGE_NOACCESS || p2 == PAGE_GUARD || p2 == 0)
            return false;
    }
    return true;
}

bool Writable(const void* p, size_t n)
{
    if (!Readable(p, n))
        return false;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!::VirtualQuery(p, &mbi, sizeof(mbi)))
        return false;
    const DWORD prot = mbi.Protect & 0xFF;
    return prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
           prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}

template <typename T> bool Rd(const void* addr, T& out)
{
    if (!Readable(addr, sizeof(T)))
        return false;
    std::memcpy(&out, addr, sizeof(T));
    return true;
}

template <typename T> bool Wr(void* addr, const T& v)
{
    if (!Writable(addr, sizeof(T)))
        return false;
    std::memcpy(addr, &v, sizeof(T));
    return true;
}

// ---------------------------------------------------------------------------
// entity list traversal
bool InitEntitySystemApi()
{
    if (!g_entitySystemSlotSearched) {
        g_entitySystemSlotSearched = true;
        g_entitySystemSlot = pattern::FindRip(
            g_clientBase, g_clientSize, kGameEntitySystemPattern, 3, 7);
    }
    if (g_entitySystemSlot) {
        uintptr_t system = 0;
        if (Rd((const void*)g_entitySystemSlot, system) && system &&
            Readable((const void*)system, sizeof(uintptr_t)))
            g_entitySystem = system;
        else
            g_entitySystem = 0;
    }
    if (!g_getBaseEntitySearched) {
        g_getBaseEntitySearched = true;
        g_getBaseEntityFn = pattern::Find(g_clientBase, g_clientSize,
                                         kGetBaseEntityPattern);
    }

    if (!g_entitySystem || !g_getBaseEntityFn ||
        !Readable((const void*)g_getBaseEntityFn, 16))
        return false;
    return true;
}

void ResetControllerCache()
{
    g_scanCursor = 0;
    g_controllerIndices.clear();
    g_activeControllers.clear();
    std::memset(g_controllerCached, 0, sizeof(g_controllerCached));
}

// ---------------------------------------------------------------------------
uintptr_t GetEntity(uintptr_t list, int idx, int layout)
{
    if (idx < 0)
        return 0;
    if (g_entityApiMode && g_entitySystem && g_getBaseEntityFn) {
        using GetBaseEntityFn = uintptr_t (*)(uintptr_t, int);
        const auto fn = reinterpret_cast<GetBaseEntityFn>(g_getBaseEntityFn);
        const uintptr_t entity = fn(g_entitySystem, idx);
        if (entity && Readable((const void*)entity, 8))
            return entity;
    }
    if (!list || (layout != 0 && layout != 1))
        return 0;
    uintptr_t chunk = 0;
    // Source 2's entity list is chunked: the high index bits select a chunk
    // pointer in the table; the low 9 bits select an entry inside that chunk.
    // Keep these indices separate (swapping them yields readable but unrelated
    // pointers and can make the layout probe report a false positive).
    const uintptr_t chunkSlot =
        list + off::kListChunkTable +
        off::kListChunkStride * (uintptr_t)(idx >> 9);
    if (!Rd((const void*)chunkSlot, chunk) || !chunk)
        return 0;
    const uintptr_t entry =
        chunk + off::kListEntryStride * (uintptr_t)(idx & (int)off::kListChunkMask);
    if (layout == 0)
        return Readable((const void*)entry, 8) ? entry : 0;
    uintptr_t e = 0;
    if (!Rd((const void*)(entry + off::kListEntityPtr), e))
        return 0;
    return e;
}

bool LooksController(uintptr_t e)
{
    if (!e || !Readable((const void*)e, 0x6C0))
        return false;
    int connected = -1;
    if (!Rd((const void*)(e + off::kConnected), connected))
        return false;
    if (connected < 0 || connected > 3)
        return false;
    unsigned char isLocal = 0xFF;
    if (!Rd((const void*)(e + off::kIsLocalController), isLocal))
        return false;
    if (isLocal > 1)
        return false;
    uintptr_t namePtr = 0;
    if (!Rd((const void*)(e + off::kPlayerName), namePtr))
        return false;
    if (namePtr == 0)
        return true; // empty name is still plausible
    char first = 0;
    if (!Rd((const void*)namePtr, first))
        return false;
    if (first == 0)
        return true;
    const unsigned char u = (unsigned char)first;
    return u >= 0x20 && u < 0x7F; // printable ASCII
}

int DetectLayout(uintptr_t list)
{
    for (int idx = 1; idx < kLayoutScan; ++idx) {
        for (int layout = 0; layout < 2; ++layout) {
            const uintptr_t e = GetEntity(list, idx, layout);
            if (LooksController(e))
                return layout;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
bool EnsureReady()
{
    // ---- module base + size from the PE header -----------------------------
    if (!g_clientBase) {
        const HMODULE mod = ::GetModuleHandleW(L"client.dll");
        if (!mod)
            return false;
        const uintptr_t base = (uintptr_t)mod;
        IMAGE_DOS_HEADER dos = {};
        if (!Rd((const void*)base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE)
            return false;
        IMAGE_NT_HEADERS64 nt = {};
        if (!Rd((const void*)(base + dos.e_lfanew), nt) ||
            nt.Signature != IMAGE_NT_SIGNATURE)
            return false;
        g_clientBase = base;
        g_clientSize = nt.OptionalHeader.SizeOfImage;
        NEXUS_LOG("game: client.dll base %p, SizeOfImage %zu",
                  (void*)g_clientBase, g_clientSize);
    }

    // Prefer the CGameEntitySystem accessor used by the working Andromeda
    // project. It handles entry layout internally and avoids probing random
    // readable addresses as possible controllers.
    if (InitEntitySystemApi()) {
        if (!g_entityApiMode) {
            ResetControllerCache();
            g_entityApiMode = true;
            NEXUS_LOG("game: CGameEntitySystem::GetBaseEntity API ready");
        }
        g_layout = 2; // GetEntity dispatches through the engine accessor.
        return true;
    }
    if (g_entityApiMode) {
        g_entityApiMode = false;
        g_layout = -1;
        ResetControllerCache();
        NEXUS_LOG("game: entity-system API unavailable; using raw-list fallback");
    } else if (!g_entityApiFallbackLogged) {
        g_entityApiFallbackLogged = true;
        NEXUS_LOG("game: Andromeda entity API signatures: system-slot=%p, GetBaseEntity=%p; trying raw-list fallback",
                  (void*)g_entitySystemSlot, (void*)g_getBaseEntityFn);
    }

    // ---- entity list pointer ------------------------------------------------
    if (g_entityList && !Readable((const void*)g_entityList, 0x10)) {
        g_entityList = 0;
        g_layout     = -1;
        g_scanCursor = 0;
        g_controllerIndices.clear();
        std::memset(g_controllerCached, 0, sizeof(g_controllerCached));
    }
    if (!g_entityList) {
        for (int i = 0; i < 3; ++i) {
            const uintptr_t store = pattern::FindRip(
                g_clientBase, g_clientSize, kEntityListPatterns[i],
                kPatternRel, kPatternLen);
            if (!store)
                continue;
            uintptr_t list = 0;
            if (!Rd((const void*)store, list) || !list)
                continue;
            if (!Readable((const void*)list, 0x10))
                continue;
            g_entityList = list;
            g_layout     = -1;
            NEXUS_LOG("game: entity list found via pattern %d -> %p", i,
                      (void*)list);
            break;
        }
        if (!g_entityList) {
            if (!g_patternFailLogged) {
                g_patternFailLogged = true;
                NEXUS_LOG("game: entity-list pattern NOT found — game features "
                          "stay disabled (menu still works)");
            }
            return false;
        }
    }

    // ---- layout autodetect --------------------------------------------------
    if (g_layout < 0) {
        const ULONGLONG now = ::GetTickCount64();
        if (g_lastLayoutProbeMs != 0 && now - g_lastLayoutProbeMs < 1000)
            return false;
        g_lastLayoutProbeMs = now;
        g_layout = DetectLayout(g_entityList);
        if (g_layout < 0) {
            if (!g_layoutFailLogged) {
                g_layoutFailLogged = true;
                NEXUS_LOG("game: raw entity-list layout probe failed");
            }
            return false;
        }
        NEXUS_LOG("game: entity list layout = %d", g_layout);
    }
    return true;
}

// ---------------------------------------------------------------------------
uintptr_t ReadPawn(uintptr_t ctrl)
{
    unsigned int h = off::kInvalidHandle;
    if (!Rd((const void*)(ctrl + off::kHHeroPawn), h) || h == off::kInvalidHandle) {
        if (!Rd((const void*)(ctrl + off::kHPawn), h) ||
            h == off::kInvalidHandle)
            return 0;
    }
    return GetEntity(g_entityList, (int)off::HandleIndex(h), g_layout);
}

void ReadName(uintptr_t ctrl, char* out, size_t cap)
{
    out[0] = 0;
    uintptr_t namePtr = 0;
    if (!Rd((const void*)(ctrl + off::kPlayerName), namePtr) || !namePtr)
        return;
    if (!Readable((const void*)namePtr, 1))
        return;
    size_t i = 0;
    for (; i + 1 < cap; ++i) {
        char c = 0;
        if (!Rd((const void*)(namePtr + i), c) || c == 0)
            break;
        const unsigned char u = (unsigned char)c;
        if (u < 0x20 || u == 0x7F)
            break; // stop at control characters
        out[i] = c;
    }
    out[i] = 0;
}

bool ReadWeaponReload(uintptr_t pawn)
{
    uintptr_t ws = 0;
    if (!Rd((const void*)(pawn + off::kWeaponServices), ws) || !ws)
        return false;
    unsigned int h = off::kInvalidHandle;
    if (!Rd((const void*)(ws + off::kHActiveWeapon), h) ||
        h == off::kInvalidHandle)
        return false;
    const uintptr_t weapon = GetEntity(g_entityList, (int)off::HandleIndex(h),
                                       g_layout);
    if (!weapon)
        return false;
    unsigned char inReload = 0;
    if (!Rd((const void*)(weapon + off::kInReload), inReload))
        return false;
    return inReload != 0;
}

void FillPlayer(uintptr_t ctrl, PlayerSnap& p)
{
    const uintptr_t pawn = ReadPawn(ctrl);
    if (!pawn)
        return;

    p.valid = true;

    unsigned char life = 1;
    Rd((const void*)(pawn + off::kLifeState), life);
    int hp = 0, hpMax = 0;
    Rd((const void*)(pawn + off::kHealth), hp);
    Rd((const void*)(pawn + off::kMaxHealth), hpMax);
    p.health    = hp;
    p.maxHealth = hpMax;
    p.alive     = (life == 0) && hp > 0;

    unsigned char team = 0;
    Rd((const void*)(pawn + off::kTeamNum), team);
    p.team    = team;
    p.isLocal = (ctrl == g_localController);
    p.isEnemy = (team != g_localTeam) && team >= 2 && g_localTeam >= 2;

    ReadName(ctrl, p.name, sizeof(p.name));

    // world origin via the game scene node
    uintptr_t node = 0;
    Rd((const void*)(pawn + off::kGameSceneNode), node);
    if (node) {
        Vec3 origin;
        if (Rd((const void*)(node + off::kAbsOrigin), origin))
            p.origin = origin;
        Vec3 vel;
        if (Rd((const void*)(pawn + off::kAbsVelocity), vel))
            p.velocity = vel;
        Vec3 vo;
        if (Rd((const void*)(pawn + off::kViewOffset), vo))
            p.viewOffsetZ = vo.z;

        // skeleton: CSkeletonInstance == the scene node of a pawn
        uintptr_t bones = 0;
        if (Rd((const void*)(node + off::kBoneArray), bones) && bones) {
            for (int b = 0; b < BoneCount; ++b) {
                const uintptr_t m =
                    bones + off::kBoneMatrixSize * (uintptr_t)kBoneIndex[b];
                Vec3 bp;
                const bool ok = Rd((const void*)(m + 0x0C), bp.x) &&
                                Rd((const void*)(m + 0x1C), bp.y) &&
                                Rd((const void*)(m + 0x2C), bp.z);
                p.bone[b]      = bp;
                p.boneValid[b] = ok;
            }
        }
    }
    ValidateBones(p); // drops garbage skeletons (ragdoll, stale buffers)
}

} // namespace

// ---------------------------------------------------------------------------
// backend API
// ---------------------------------------------------------------------------
bool Capture(Snapshot& out)
{
    out.Clear();
    if (!EnsureReady())
        return false;

    const uintptr_t list = g_entityList;

    // Discover controllers incrementally: the old implementation queried up to
    // 2048 entries twice on every Present. VirtualQuery per field made that
    // path dominate frame time. A rolling scan bounds discovery to 128 slots;
    // discovered indices are then revalidated and reused each frame.
    for (int n = 0; n < kScanBatch; ++n) {
        const int idx = g_scanCursor++;
        if (g_scanCursor >= kScanMax)
            g_scanCursor = 0;
        const uintptr_t ctrl = GetEntity(list, idx, g_layout);
        if (!LooksController(ctrl))
            continue;
        int connected = 0;
        if (!Rd((const void*)(ctrl + off::kConnected), connected) ||
            connected != 2 || g_controllerCached[idx])
            continue;
        g_controllerCached[idx] = true;
        g_controllerIndices.push_back(idx);
    }

    std::vector<uintptr_t>& controllers = g_activeControllers;
    controllers.clear();
    controllers.reserve(g_controllerIndices.size());
    for (auto it = g_controllerIndices.begin(); it != g_controllerIndices.end();) {
        const int idx = *it;
        const uintptr_t ctrl = GetEntity(list, idx, g_layout);
        int connected = 0;
        if (!LooksController(ctrl) ||
            !Rd((const void*)(ctrl + off::kConnected), connected) ||
            connected != 2) {
            g_controllerCached[idx] = false;
            it = g_controllerIndices.erase(it);
            continue;
        }
        controllers.push_back(ctrl);
        ++it;
    }

    g_localController = 0;
    g_localPawn       = 0;
    g_localTeam       = -1;
    for (const uintptr_t ctrl : controllers) {
        unsigned char isLocal = 0;
        if (Rd((const void*)(ctrl + off::kIsLocalController), isLocal) && isLocal) {
            g_localController = ctrl;
            break;
        }
    }
    if (!g_localController) {
        LogCaptureStatus("local-controller-missing");
        return false;
    }
    g_localPawn = ReadPawn(g_localController);
    if (!g_localPawn) {
        LogCaptureStatus("local-pawn-missing");
        return false;
    }
    unsigned char lt = 0;
    if (Rd((const void*)(g_localPawn + off::kTeamNum), lt))
        g_localTeam = lt;

    // ---- camera (local pawn + m_angClientCamera + m_iDesiredFOV) -----------
    {
        Camera& cam = out.camera;
        cam.width  = g_vpW;
        cam.height = g_vpH;

        uintptr_t node = 0;
        Rd((const void*)(g_localPawn + off::kGameSceneNode), node);
        Vec3 origin;
        if (node)
            Rd((const void*)(node + off::kAbsOrigin), origin);
        Vec3 vo;
        if (Rd((const void*)(g_localPawn + off::kViewOffset), vo))
            cam.pos = origin + Vec3(0, 0, vo.z);
        else
            cam.pos = origin + Vec3(0, 0, kDefaultViewOffsetZ);

        float pitch = 0, yaw = 0;
        Rd((const void*)(g_localPawn + off::kAngClientCamera), pitch);
        Rd((const void*)(g_localPawn + off::kAngClientCamera + 4), yaw);
        cam.pitch = pitch;
        cam.yaw   = yaw;

        int fov = 0;
        if (Rd((const void*)(g_localController + off::kDesiredFOV), fov))
            cam.fovX = (fov >= 60 && fov <= 140) ? (float)fov : 90.0f;

        cam.valid = (g_vpW > 0 && g_vpH > 0);
    }

    // ---- players ------------------------------------------------------------
    int count = 0;
    for (const uintptr_t ctrl : controllers) {
        if (count >= kMaxPlayers)
            break;
        FillPlayer(ctrl, out.players[count]);
        if (out.players[count].valid)
            ++count;
    }
    out.count = count;

    int alive = 0;
    int enemies = 0;
    for (int i = 0; i < count; ++i) {
        const PlayerSnap& p = out.players[i];
        alive += p.valid && p.alive ? 1 : 0;
        enemies += p.valid && p.alive && p.isEnemy ? 1 : 0;
    }

    out.weaponInReload = ReadWeaponReload(g_localPawn);
    out.valid          = out.camera.valid;
    LogCaptureStatus(out.valid && count > 0 ? "ok" : "no-players", count,
                     alive, enemies, g_vpW, g_vpH);
    return count > 0;
}

// ---------------------------------------------------------------------------
void SetCameraAngles(float pitch, float yaw)
{
    if (!g_localPawn)
        return;
    const float p[2] = {pitch, yaw};
    for (const uintptr_t base : {off::kAngClientCamera, off::kVAngle,
                                 off::kAngEyeAngles}) {
        Wr((void*)(g_localPawn + base), p[0]);
        Wr((void*)(g_localPawn + base + 4), p[1]);
    }
}

// ---------------------------------------------------------------------------
void TickMisc(const Snapshot& snap)
{
    const nexus::State& st = nexus::GetState();

    // ---- FOV changer: write m_iDesiredFOV of the local controller ----------
    if (st.fovChanger && g_localController) {
        const int fov = (int)std::lround(st.fovValue);
        Wr((void*)(g_localController + off::kDesiredFOV), fov);
    }

    // ---- Auto Active Reload: synthetic 'R' every 90 ms while reloading -----
    if (st.autoActiveReload && snap.weaponInReload) {
        const ULONGLONG now = ::GetTickCount64();
        if (now - g_lastReloadMs >= kReloadIntervalMs) {
            g_lastReloadMs = now;
            const BYTE vk   = 'R';
            const BYTE scan = (BYTE)::MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
            ::keybd_event(vk, scan, 0, 0);
            ::keybd_event(vk, scan, KEYEVENTF_KEYUP, 0);
        }
    }
}

// ---------------------------------------------------------------------------
void SetViewport(int width, int height)
{
    g_vpW = width;
    g_vpH = height;
}

const char* BackendName() { return "deadlock"; }

} // namespace game
