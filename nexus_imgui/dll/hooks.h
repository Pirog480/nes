// ============================================================================
//  hooks.h — the DXGI present hook of nexus.dll.
//
//  Strategy (educational, no anti-cheat bypass): create a throw-away D3D11
//  device + swap chain, read the *shared* IDXGISwapChain vtable out of it and
//  patch two entries:
//      Present        = index 8
//      ResizeBuffers  = index 13
//  Because the vtable is shared by every IDXGISwapChain of the process, the
//  game's own swap chain then runs through our hooks. The dummy objects are
//  destroyed right after patching.
// ============================================================================
#pragma once

namespace nexus_hooks {

// Creates the dummy device, patches the vtable and subclasses the window.
// Safe to call once; returns false (and logs) on failure.
bool Install();

// Unloading nexus.dll is NOT supported: the vtable stays patched for the
// lifetime of the process. Documented in the README.
bool IsInstalled();

} // namespace nexus_hooks
