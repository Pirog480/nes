// ============================================================================
//  dllmain.cpp — entry point of nexus.dll (educational Deadlock trainer).
//
//  On attach we only spawn a worker thread: doing real work inside DllMain is
//  a loader-lock hazard. The thread waits (up to 30 s) for client.dll to be
//  mapped and then installs the DXGI hooks.
//
//  NOTE: unloading the DLL is NOT supported. The swap-chain vtable stays
//  patched until the game exits (see README, "Сборка DLL и инжект").
// ============================================================================
#include <windows.h>

#include "dll_log.h"
#include "hooks.h"

namespace {

DWORD WINAPI WorkerThread(LPVOID /*unused*/)
{
    NEXUS_LOG("nexus.dll: worker started, waiting for client.dll (30 s)");

    HMODULE       mod   = nullptr;
    const DWORD   start = ::GetTickCount();
    while (!mod && (::GetTickCount() - start) < 30000u) {
        mod = ::GetModuleHandleW(L"client.dll");
        if (!mod)
            ::Sleep(250);
    }
    if (!mod) {
        NEXUS_LOG("nexus.dll: client.dll never appeared — staying idle");
        return 1;
    }
    NEXUS_LOG("nexus.dll: client.dll mapped at %p", (void*)mod);

    if (!nexus_hooks::Install()) {
        NEXUS_LOG("nexus.dll: hook installation failed");
        return 2;
    }
    NEXUS_LOG("nexus.dll: installed; press Insert in game to open the menu");
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        ::DisableThreadLibraryCalls(hinst);
        NEXUS_LOG("nexus.dll: attach (pid %lu)",
                  (unsigned long)::GetCurrentProcessId());
        {
            const HANDLE t =
                ::CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
            if (t)
                ::CloseHandle(t);
        }
        break;

    case DLL_PROCESS_DETACH:
        // deliberately empty: unload is unsupported
        break;

    default:
        break;
    }
    return TRUE;
}
