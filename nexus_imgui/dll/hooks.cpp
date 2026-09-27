// ============================================================================
//  hooks.cpp — DXGI swap-chain vtable hook + the per-frame UI pipeline.
//
//  Frame order (important — see esp.h):
//      ImGui_ImplWin32_NewFrame / ImGui_ImplDX11_NewFrame / ImGui::NewFrame
//      -> game::Capture(snap)
//      -> nexus_aim::Tick(snap)
//      -> game::TickMisc(snap)          (FOV changer / auto active reload)
//      -> nexus::DrawMenu()             (FOV circles + transparent menu overlay)
//      -> nexus_esp::Render(snap)       (background list, above FOV circles)
//      -> ImGui::Render() + ImGui_ImplDX11_RenderDrawData() onto our RTV
//      -> the original Present
// ============================================================================
#include "hooks.h"
#include "dll_log.h"

#include "../src/aim.h"
#include "../src/esp.h"
#include "../src/game.h"
#include "../src/nexus_menu.h"

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

// ImGui_ImplWin32_WndProcHandler is intentionally NOT declared by the backend
// header (it sits inside an `#if 0` block there), so forward-declare it.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace nexus_hooks {
namespace {

// IDXGISwapChain vtable slots we patch
constexpr int kSlotPresent       = 8;
constexpr int kSlotResizeBuffers = 13;

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT,
                                                    UINT, DXGI_FORMAT, UINT);

PresentFn       g_origPresent = nullptr;
ResizeBuffersFn g_origResize  = nullptr;
bool            g_installed   = false;
bool            g_imguiReady  = false;

HWND                      g_hwnd = nullptr;
WNDPROC                   g_oldWndProc = nullptr;
ID3D11Device*             g_dev = nullptr;
ID3D11DeviceContext*      g_ctx = nullptr;
ID3D11RenderTargetView*   g_rtv = nullptr;
int                       g_bbW = 0, g_bbH = 0;

// ---------------------------------------------------------------------------
void ReleaseRtv()
{
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

bool CreateRtv(IDXGISwapChain* sc)
{
    ReleaseRtv();
    if (!g_dev)
        return false;
    ID3D11Texture2D* back = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)))
        return false;
    const HRESULT hr = g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return SUCCEEDED(hr) && g_rtv != nullptr;
}

// ---------------------------------------------------------------------------
// window procedure: ImGui first, then swallow input while the menu is open
// ---------------------------------------------------------------------------
bool IsInputMessage(UINT msg)
{
    switch (msg) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_CHAR: case WM_SYSCHAR: case WM_DEADCHAR:
    case WM_MOUSEMOVE: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
    case WM_INPUT:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // 1) ImGui always sees the message first
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return 0;

    // 2) while the menu is open the game must not receive input
    if (nexus::GetState().menuOpen && IsInputMessage(msg))
        return 0;

    return ::CallWindowProcW(g_oldWndProc, hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
bool InitImGui(IDXGISwapChain* sc)
{
    DXGI_SWAP_CHAIN_DESC desc = {};
    if (FAILED(sc->GetDesc(&desc)))
        return false;
    g_hwnd = desc.OutputWindow;
    g_bbW  = (int)desc.BufferDesc.Width;
    g_bbH  = (int)desc.BufferDesc.Height;

    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_dev))) {
        NEXUS_LOG("hooks: GetDevice failed");
        return false;
    }
    g_dev->GetImmediateContext(&g_ctx);
    if (!g_ctx) {
        NEXUS_LOG("hooks: no immediate context");
        return false;
    }
    if (!CreateRtv(sc)) {
        NEXUS_LOG("hooks: CreateRenderTargetView failed");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    // Segoe UI when available, the embedded ProggyClean otherwise
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf",
                                                15.0f);
    if (!font) {
        io.Fonts->Clear();
        io.Fonts->AddFontDefault();
        NEXUS_LOG("hooks: segoeui.ttf missing, using the built-in font");
    }

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    g_oldWndProc = (WNDPROC)::SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC,
                                                (LONG_PTR)HookedWndProc);
    game::SetViewport(g_bbW, g_bbH);

    g_imguiReady = true;
    NEXUS_LOG("hooks: ImGui ready (hwnd %p, %dx%d, backend=%s)",
              (void*)g_hwnd, g_bbW, g_bbH, game::BackendName());
    return true;
}

// ---------------------------------------------------------------------------
void RenderFrame(IDXGISwapChain* sc)
{
    ImGui_ImplWin32_NewFrame();
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    if (nexus::GetState().menuOpen) {
        ImGui::GetIO().MouseDrawCursor = true;
        ::ClipCursor(nullptr);
    } else {
        ImGui::GetIO().MouseDrawCursor = false;
    }

    game::Snapshot snap;
    game::Capture(snap);        // reads the live game (stub: nothing)
    nexus_aim::Tick(snap);      // vector aim (writes camera angles)
    game::TickMisc(snap);       // FOV changer / auto active reload
    nexus::DrawMenu();          // background + menu window
    nexus_esp::Render(snap);    // ESP on top of the background, below the menu
    ImGui::Render();

    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || !g_rtv || !g_ctx)
        return;

    ID3D11RenderTargetView* prevRtv = nullptr;
    g_ctx->OMGetRenderTargets(1, &prevRtv, nullptr);
    UINT          vpCount = 1;
    D3D11_VIEWPORT prevVp = {};
    g_ctx->RSGetViewports(&vpCount, &prevVp);

    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    D3D11_VIEWPORT vp = {};
    vp.Width    = (FLOAT)g_bbW;
    vp.Height   = (FLOAT)g_bbH;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_ctx->RSSetViewports(1, &vp);

    ImGui_ImplDX11_RenderDrawData(dd);

    g_ctx->OMSetRenderTargets(1, &prevRtv, nullptr);
    if (prevRtv)
        prevRtv->Release();
    g_ctx->RSSetViewports(vpCount, &prevVp);
    (void)sc;
}

// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* sc, UINT syncInterval,
                                        UINT flags)
{
    if (!g_imguiReady)
        InitImGui(sc); // failure just leaves the game untouched
    if (g_imguiReady)
        RenderFrame(sc);
    return g_origPresent(sc, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* sc, UINT count,
                                              UINT width, UINT height,
                                              DXGI_FORMAT format, UINT flags)
{
    ReleaseRtv();
    const HRESULT hr = g_origResize(sc, count, width, height, format, flags);
    if (g_imguiReady && SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC d = {};
        if (SUCCEEDED(sc->GetDesc(&d))) {
            g_bbW = (int)d.BufferDesc.Width;
            g_bbH = (int)d.BufferDesc.Height;
            game::SetViewport(g_bbW, g_bbH);
        }
        CreateRtv(sc);
    }
    return hr;
}

// ---------------------------------------------------------------------------
bool PatchSlot(void** vtable, int index, void* hook, void** original)
{
    void** slot = vtable + index;
    DWORD  oldProt = 0;
    if (!::VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt))
        return false;
    *original = *slot;
    *slot     = hook;
    DWORD tmp = 0;
    ::VirtualProtect(slot, sizeof(void*), oldProt, &tmp);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
bool IsInstalled() { return g_installed; }

bool Install()
{
    if (g_installed)
        return true;

    // ---- throw-away window -------------------------------------------------
    WNDCLASSEXW wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.lpfnWndProc    = ::DefWindowProcW;
    wc.hInstance      = ::GetModuleHandleW(nullptr);
    wc.lpszClassName  = L"NexusDummyWindow";
    ::RegisterClassExW(&wc);

    const HWND hw = ::CreateWindowExW(0, wc.lpszClassName, L"",
                                      WS_OVERLAPPEDWINDOW, 100, 100, 320, 240,
                                      nullptr, nullptr, wc.hInstance, nullptr);
    if (!hw) {
        NEXUS_LOG("hooks: cannot create the dummy window (%lu)",
                  (unsigned long)::GetLastError());
        return false;
    }

    // ---- throw-away device + swap chain ------------------------------------
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount              = 2;
    sd.BufferDesc.Width         = 320;
    sd.BufferDesc.Height        = 240;
    sd.BufferDesc.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate   = {60, 1};
    sd.BufferUsage              = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow             = hw;
    sd.SampleDesc.Count         = 1;
    sd.SampleDesc.Quality       = 0;
    sd.Windowed                 = TRUE;
    sd.SwapEffect               = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device*           dev = nullptr;
    ID3D11DeviceContext*    ctx = nullptr;
    IDXGISwapChain*         sc  = nullptr;
    D3D_FEATURE_LEVEL       fl  = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = ::D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    if (FAILED(hr)) {
        NEXUS_LOG("hooks: hardware device failed (0x%08lx), trying WARP",
                  (unsigned long)hr);
        hr = ::D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    }
    if (FAILED(hr) || !sc) {
        NEXUS_LOG("hooks: D3D11CreateDeviceAndSwapChain failed (0x%08lx)",
                  (unsigned long)hr);
        ::DestroyWindow(hw);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    // ---- patch the shared vtable -------------------------------------------
    void** vtable = *reinterpret_cast<void***>(sc);
    const bool okP = PatchSlot(vtable, kSlotPresent, (void*)&HookedPresent,
                               (void**)&g_origPresent);
    const bool okR = PatchSlot(vtable, kSlotResizeBuffers,
                               (void*)&HookedResizeBuffers,
                               (void**)&g_origResize);

    // the dummies are no longer needed; the patch lives in the module that
    // owns the vtable, so it survives their destruction
    sc->Release();
    dev->Release();
    ctx->Release();
    ::DestroyWindow(hw);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (!okP || !okR || !g_origPresent || !g_origResize) {
        NEXUS_LOG("hooks: vtable patch failed (present=%d resize=%d)", (int)okP,
                  (int)okR);
        return false;
    }

    g_installed = true;
    NEXUS_LOG("hooks: Present(%d)/ResizeBuffers(%d) patched", kSlotPresent,
              kSlotResizeBuffers);
    return true;
}

} // namespace nexus_hooks
