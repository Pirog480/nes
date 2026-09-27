#define IMGUI_DEFINE_MATH_OPERATORS
// ============================================================================
//  nexus_menu.cpp — NEXUS menu: layout, tabs, overlays, input handling.
//  Direct port of the structure/behaviour of uusa.html.
// ============================================================================
#include "nexus_menu.h"
#include "nexus_widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace nexus {

// ---------------------------------------------------------------------------
// Palettes (from the CSS variables of uusa.html)
// ---------------------------------------------------------------------------
namespace {

struct Palette {
    ImVec4 bgApp, bgMenu, bgCard, bgCardHover, bgInput;
    ImVec4 border, borderGlow, text, textSecondary, shadow;
};

constexpr ImVec4 RGB(unsigned char r, unsigned char g, unsigned char b,
                     float a = 1.0f)
{
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

const Palette kDark = {
    RGB(0x0e, 0x0e, 0x12),
    RGB(0x14, 0x14, 0x1c, 0.75f),
    RGB(0x1e, 0x1e, 0x2a, 0.60f),
    RGB(0x28, 0x28, 0x37, 0.80f),
    RGB(0x00, 0x00, 0x00, 0.30f),
    RGB(0xff, 0xff, 0xff, 0.08f),
    RGB(0xff, 0xff, 0xff, 0.15f),
    RGB(0xe0, 0xe0, 0xe6),
    RGB(0x8e, 0x8e, 0x9a),
    RGB(0x00, 0x00, 0x00, 0.20f),
};

const Palette kLight = {
    RGB(0xf0, 0xf2, 0xf5),
    RGB(0xeb, 0xeb, 0xf0, 0.65f),
    RGB(0xff, 0xff, 0xff, 0.70f),
    RGB(0xff, 0xff, 0xff, 0.90f),
    RGB(0x00, 0x00, 0x00, 0.05f),
    RGB(0x00, 0x00, 0x00, 0.08f),
    RGB(0x00, 0x00, 0x00, 0.15f),
    RGB(0x1a, 0x1a, 0x1e),
    RGB(0x6e, 0x6e, 0x7a),
    RGB(0x00, 0x00, 0x00, 0.05f),
};

// swatch sets from the HTML (data-color attributes)
const ImVec4 kAccentColors[5] = {
    RGB(0x3b, 0x82, 0xf6), RGB(0x10, 0xb9, 0x81), RGB(0x8b, 0x5c, 0xf6),
    RGB(0xef, 0x44, 0x44), RGB(0xf5, 0x9e, 0x0b)};
const ImVec4 kFovNormalColors[4] = {
    RGB(0x3b, 0x82, 0xf6), RGB(0x10, 0xb9, 0x81), RGB(0xef, 0x44, 0x44),
    RGB(0xff, 0xff, 0xff)};
const ImVec4 kFovPsilentColors[4] = {
    RGB(0xff, 0x00, 0xff), RGB(0xf5, 0x9e, 0x0b), RGB(0xec, 0x48, 0x99),
    RGB(0xff, 0xff, 0xff)};
const ImVec4 kAllyColors[3] = {RGB(0x3b, 0x82, 0xf6), RGB(0x10, 0xb9, 0x81),
                               RGB(0x8b, 0x5c, 0xf6)};
const ImVec4 kEnemyColors[3] = {RGB(0xef, 0x44, 0x44), RGB(0xf5, 0x9e, 0x0b),
                                RGB(0xec, 0x48, 0x99)};
const ImVec4 kHeroColors[12] = {
    RGB(0x3b, 0x82, 0xf6), RGB(0x10, 0xb9, 0x81), RGB(0x8b, 0x5c, 0xf6),
    RGB(0xef, 0x44, 0x44), RGB(0xf5, 0x9e, 0x0b), RGB(0xec, 0x48, 0x99),
    RGB(0x14, 0xb8, 0xa6), RGB(0xf9, 0x73, 0x16), RGB(0x63, 0x66, 0xf1),
    RGB(0x84, 0xcc, 0x16), RGB(0x06, 0xb6, 0xd4), RGB(0xe1, 0x1d, 0x48)};
const ImVec4 kGreen = RGB(0x10, 0xb9, 0x81);

// ---------------------------------------------------------------------------
// state & transient values
// ---------------------------------------------------------------------------

State g_state;

float  g_openAnim     = 1.0f;
float  g_heightAnim   = 520.0f;
float  g_navIndPos    = -1.0f; // relative to sidebar top (scale-independent)
ImVec2 g_menuPos(0, 0);
bool   g_menuPosInit  = false;
bool   g_wasOpen      = false;
ImVec2 g_dragGrab(0, 0);
float  g_fps          = 60.0f;
float  g_fovNAlpha    = 0.0f;
float  g_fovPAlpha    = 0.0f;
bool   g_refocusMenu  = false;

struct Toast {
    std::string text;
    double      born;
};
std::vector<Toast> g_toasts;

enum Capture { CAP_NONE, CAP_ACTIVATE, CAP_MENU };
Capture g_capture  = CAP_NONE;
bool    g_armMouse = false;

ImVec2 g_panelPos(0, 0), g_panelSize(0, 0);
bool   g_panelRectValid = false;

ImU32 Col(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

ImU32 Alpha(const ImVec4& c, float a)
{
    ImVec4 o = c;
    o.w      = a;
    return Col(o);
}

const Palette& Pal() { return g_state.theme ? kLight : kDark; }

void CloseHeroPanel()
{
    g_state.heroOpen = -1;
    g_refocusMenu    = true;
}

// Insert an explicit margin of 'css' px after the last item.
void MarginAfter(float css)
{
    const float want   = S(css);
    const float auto_g = ImGui::GetStyle().ItemSpacing.y;
    if (want > auto_g)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (want - auto_g));
}

// ---------------------------------------------------------------------------
// style
// ---------------------------------------------------------------------------

void ApplyStyle(float scale, float alpha_factor)
{
    static ImGuiStyle base;
    static bool       inited = false;
    if (!inited) {
        base                   = ImGuiStyle();
        base.WindowRounding    = 16.0f;
        base.WindowBorderSize  = 1.0f;
        base.ChildRounding     = 12.0f;
        base.ChildBorderSize   = 1.0f;
        base.FrameRounding     = 8.0f;
        base.PopupRounding     = 8.0f;
        base.ScrollbarRounding = 8.0f;
        base.GrabRounding      = 7.0f;
        base.WindowPadding     = ImVec2(16, 16);
        base.FramePadding      = ImVec2(10, 7);
        base.ItemSpacing       = ImVec2(6, 8);
        base.ItemInnerSpacing  = ImVec2(6, 4);
        base.FrameBorderSize   = 1.0f;
        base.WindowBorderSize  = 1.0f;
        inited                 = true;
    }

    ImGuiStyle s = base;
    s.ScaleAllSizes(scale);

    const Palette& p   = Pal();
    const ImVec4   acc = kAccentColors[g_state.accentIdx];
    auto           A   = [&](ImVec4 c) {
        c.w *= alpha_factor;
        return c;
    };
    ImVec4* C = s.Colors;

    C[ImGuiCol_Text]                 = A(p.text);
    C[ImGuiCol_TextDisabled]         = A(p.textSecondary);
    C[ImGuiCol_WindowBg]             = A(p.bgMenu);
    C[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    C[ImGuiCol_PopupBg]              = A(p.bgMenu);
    C[ImGuiCol_Border]               = A(p.border);
    C[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    C[ImGuiCol_FrameBg]              = A(p.bgInput);
    C[ImGuiCol_FrameBgHovered]       = A(p.bgCardHover);
    C[ImGuiCol_FrameBgActive]        = A(p.bgCardHover);
    C[ImGuiCol_TitleBg]              = A(p.bgMenu);
    C[ImGuiCol_TitleBgActive]        = A(p.bgMenu);
    C[ImGuiCol_TitleBgCollapsed]     = A(p.bgMenu);
    C[ImGuiCol_MenuBarBg]            = A(p.bgMenu);
    C[ImGuiCol_ScrollbarBg]          = A(p.bgMenu);
    C[ImGuiCol_ScrollbarGrab]        = A(p.textSecondary);
    C[ImGuiCol_ScrollbarGrabHovered] = A(p.text);
    C[ImGuiCol_ScrollbarGrabActive]  = A(p.text);
    C[ImGuiCol_CheckMark]            = A(acc);
    C[ImGuiCol_SliderGrab]           = A(acc);
    C[ImGuiCol_SliderGrabActive]     = A(acc);
    C[ImGuiCol_Button]               = A(p.bgInput);
    C[ImGuiCol_ButtonHovered]        = A(p.bgCardHover);
    C[ImGuiCol_ButtonActive]         = A(p.bgCardHover);
    C[ImGuiCol_Header]               = A(acc);
    C[ImGuiCol_HeaderHovered]        = A(acc);
    C[ImGuiCol_HeaderActive]         = A(acc);
    C[ImGuiCol_Separator]            = A(p.border);
    C[ImGuiCol_ResizeGrip]           = A(p.bgInput);
    C[ImGuiCol_ResizeGripHovered]    = A(p.bgCardHover);
    C[ImGuiCol_ResizeGripActive]     = A(p.bgCardHover);
    C[ImGuiCol_Tab]                  = A(p.bgInput);
    C[ImGuiCol_TabHovered]           = A(p.bgCardHover);
    C[ImGuiCol_TabSelected]          = A(p.bgCardHover);
    C[ImGuiCol_TabSelectedOverline]  = A(acc);
    C[ImGuiCol_TableBorderLight]     = A(p.border);
    C[ImGuiCol_TableBorderStrong]    = A(p.border);
    C[ImGuiCol_NavHighlight]         = A(acc);
    C[ImGuiCol_ModalWindowDimBg]     = ImVec4(0, 0, 0, 0.5f);

    ImGui::GetStyle()             = s;
    ImGui::GetIO().FontGlobalScale = scale;
}

void FillWidgetTheme(float alpha_factor)
{
    const Palette& p   = Pal();
    const ImVec4   acc = kAccentColors[g_state.accentIdx];
    auto           A   = [&](ImVec4 c) {
        c.w *= alpha_factor;
        return c;
    };
    g_theme.accent        = A(acc);
    g_theme.textPrimary   = A(p.text);
    g_theme.textSecondary = A(p.textSecondary);
    g_theme.bgInput       = A(p.bgInput);
    g_theme.bgCard        = A(p.bgCard);
    g_theme.bgCardHover   = A(p.bgCardHover);
    g_theme.border        = A(p.border);
    g_theme.borderGlow    = A(p.borderGlow);
    g_theme.shadow        = A(p.shadow);
}

// ---------------------------------------------------------------------------
// FOV circles (fixed overlay, centered on the screen)
// ---------------------------------------------------------------------------

void FovCircle(ImDrawList* dl, float diameter, const ImVec4& color, float a)
{
    if (a <= 0.01f)
        return;
    const ImVec2 ds    = ImGui::GetIO().DisplaySize;
    const ImVec2 c     = ImVec2(ds.x * 0.5f, ds.y * 0.5f);
    const float  r     = diameter * 0.5f;
    const float  glow  = 15.0f;
    for (int j = 0; j < 8; ++j) {
        const float off = -glow * 0.5f + glow * j / 7.0f;
        const float w   = 1.0f - std::fabs(off) / (glow * 0.5f);
        dl->AddCircle(c, r + off, Alpha(color, a * 0.10f * w * w), 0, 2.5f);
    }
    dl->AddCircle(c, r, Alpha(color, a), 0, 2.0f);
}

void DrawFovCircles()
{
    auto* dl = ImGui::GetBackgroundDrawList();

    g_fovNAlpha = Animate(g_fovNAlpha,
                          g_state.normal.fovShown
                              ? g_state.normal.opacity / 100.0f
                              : 0.0f,
                          10.0f);
    FovCircle(dl, g_state.normal.fov * 4.0f,
              kFovNormalColors[g_state.normal.colorIdx], g_fovNAlpha);

    g_fovPAlpha = Animate(g_fovPAlpha,
                          g_state.psilent.fovShown
                              ? g_state.psilent.opacity / 100.0f
                              : 0.0f,
                          10.0f);
    FovCircle(dl, g_state.psilent.fov * 4.0f,
              kFovPsilentColors[g_state.psilent.colorIdx], g_fovPAlpha);
}

// ---------------------------------------------------------------------------
// keybind capture
// ---------------------------------------------------------------------------

void ApplyCapture(const char* text, ImGuiKey key)
{
    if (g_capture == CAP_ACTIVATE) {
        g_state.activationKey = text;
    } else if (g_capture == CAP_MENU) {
        g_state.menuKey    = text;
        g_state.menuKeyKey = key;
    }
    g_capture  = CAP_NONE;
    g_armMouse = false;
}

void UpdateCapture()
{
    if (g_capture == CAP_NONE)
        return;

    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const ImGuiKey key = (ImGuiKey)k;
        if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY)
            continue;
        if (!ImGui::IsKeyPressed(key, false))
            continue;
        if (key == ImGuiKey_Escape) {
            ApplyCapture("\xe2\x80\x93", (ImGuiKey)0); // "–", like the HTML
        } else {
            const char* name = ImGui::GetKeyName(key);
            char        buf[64];
            if (!name || !name[0])
                std::snprintf(buf, sizeof(buf), "KEY%d", (int)key);
            else
                std::snprintf(buf, sizeof(buf), "%s", name);
            ApplyCapture(buf, key);
        }
        return;
    }

    if (g_armMouse) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            g_armMouse = false;
        return;
    }
    const char* mb = nullptr;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        mb = "MB1";
    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        mb = "MB3";
    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        mb = "MB2";
    if (mb)
        ApplyCapture(mb, (ImGuiKey)0);
}

void StartCapture(Capture target)
{
    g_capture  = target;
    g_armMouse = true;
}

// ---------------------------------------------------------------------------
// header
// ---------------------------------------------------------------------------

void DrawGlyphClose(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col, float t)
{
    const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    const float  k = mx.x - mn.x;
    dl->AddLine(ImVec2(c.x - k * 0.35f, c.y - k * 0.35f),
                ImVec2(c.x + k * 0.35f, c.y + k * 0.35f), col, t);
    dl->AddLine(ImVec2(c.x + k * 0.35f, c.y - k * 0.35f),
                ImVec2(c.x - k * 0.35f, c.y + k * 0.35f), col, t);
    (void)mn; (void)mx;
}

void DrawGlyphMinimize(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx,
                       ImU32 col, float t)
{
    const float cy = (mn.y + mx.y) * 0.5f;
    dl->AddLine(ImVec2(mn.x + (mx.x - mn.x) * 0.15f, cy),
                ImVec2(mx.x - (mx.x - mn.x) * 0.15f, cy), col, t);
}

void DrawHeader(float win_w)
{
    const ImGuiStyle& st   = ImGui::GetStyle();
    const float        b    = st.WindowBorderSize;
    const float        h    = S(48.0f);
    const ImVec2       wpos = ImGui::GetWindowPos();
    auto*              dl   = ImGui::GetWindowDrawList();
    ImFont*     font = ImGui::GetFont();
    const float        fs   = ImGui::GetFontSize();
    const float        t    = S(1.5f);

    const float btn    = S(14.0f);
    const float pad    = S(16.0f);
    const float gap    = S(12.0f);
    const float close_x = win_w - b - pad - btn;
    const float min_x   = close_x - gap - btn;

    // Drag zone is submitted first so buttons submitted below take precedence.
    ImGui::SetCursorPos(ImVec2(b, b));
    ImGui::InvisibleButton("##hdr_drag", ImVec2(win_w - 2.0f * b, h));
    const bool dragHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated())
        g_dragGrab = ImGui::GetMousePos() - ImGui::GetWindowPos();
    if (ImGui::IsItemActive())
        g_menuPos = ImGui::GetMousePos() - g_dragGrab;
    if (dragHovered || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // fps counter
    {
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%d FPS", (int)(g_fps + 0.5f));
        const ImVec2 fsz = ImGui::CalcTextSize(fps);
        ImGui::SetCursorPos(
            ImVec2(min_x - gap - fsz.x, b + (h - fsz.y) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, g_theme.textSecondary);
        ImGui::TextUnformatted(fps);
        ImGui::PopStyleColor();
    }

    // close button
    {
        ImGui::SetCursorPos(ImVec2(close_x, b + (h - btn) * 0.5f));
        ImGui::InvisibleButton("##hdr_close", ImVec2(btn, btn));
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        const ImU32  col = ImGui::IsItemHovered() ? Col(g_theme.textPrimary)
                                                  : Col(g_theme.textSecondary);
        DrawGlyphClose(dl, mn, mx, col, t);
        if (ImGui::IsItemClicked())
            g_state.menuOpen = false;
    }
    // minimize button
    {
        ImGui::SetCursorPos(ImVec2(min_x, b + (h - btn) * 0.5f));
        ImGui::InvisibleButton("##hdr_min", ImVec2(btn, btn));
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        const ImU32  col = ImGui::IsItemHovered() ? Col(g_theme.textPrimary)
                                                  : Col(g_theme.textSecondary);
        DrawGlyphMinimize(dl, mn, mx, col, t);
        if (ImGui::IsItemClicked())
            g_state.minimized = !g_state.minimized;
    }

    // brand (draw list only — stays "window background" for dragging)
    {
        const float dr = S(4.0f);
        const ImVec2 dc(wpos.x + b + pad + dr, wpos.y + b + h * 0.5f);
        const float  phase = std::fmod((float)ImGui::GetTime(), 2.0f) / 2.0f;
        if (phase < 0.7f && g_animSpeed > 0.0f) {
            const float pr = phase / 0.7f;
            dl->AddCircleFilled(dc, dr + S(6.0f) * pr,
                                Alpha(g_theme.accent, 0.5f * (1.0f - pr)));
        }
        dl->AddCircleFilled(dc, dr, Col(g_theme.accent));
        dl->AddText(font, fs,
                    ImVec2(dc.x + dr + S(8.0f), wpos.y + b + (h - fs) * 0.5f),
                    Col(g_theme.textPrimary), "NEXUS");
    }

    // header separator
    dl->AddLine(ImVec2(wpos.x + b, wpos.y + b + h),
                ImVec2(wpos.x + win_w - b, wpos.y + b + h),
                Col(g_theme.border));

    // cursor below the header
    ImGui::SetCursorPos(ImVec2(b, b + h));
}

// ---------------------------------------------------------------------------
// sidebar
// ---------------------------------------------------------------------------

void DrawSidebar()
{
    static const char* const names[5] = {"Aim", "Visuals", "Heroes", "Misc",
                                         "Settings"};
    static const NavIcon     icons[5] = {NavIcon_Aim, NavIcon_Visuals,
                                         NavIcon_Heroes, NavIcon_Misc,
                                         NavIcon_Settings};

    const float  item_h = S(40.0f);
    const ImVec2 org    = ImGui::GetCursorScreenPos();
    auto*        dl     = ImGui::GetWindowDrawList();

    for (int i = 0; i < 5; ++i) {
        ImGui::PushID(i);
        ImGui::InvisibleButton("##nav", ImVec2(S(180.0f), item_h));
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        if (clicked) {
            g_state.activeTab = i;
            CloseHeroPanel();
        }

        const ImVec2 r0     = ImGui::GetItemRectMin();
        const bool   active = (g_state.activeTab == i);
        const ImVec4 col =
            (active || hovered) ? g_theme.textPrimary : g_theme.textSecondary;

        DrawNavIcon(dl, icons[i],
                    ImVec2(r0.x + S(20.0f), r0.y + (item_h - S(18.0f)) * 0.5f),
                    S(18.0f), Col(col));
        const char*  nm = names[i];
        const ImVec2 ts = ImGui::CalcTextSize(nm);
        dl->AddText(
            ImVec2(r0.x + S(20.0f) + S(18.0f) + S(12.0f),
                   r0.y + (item_h - ts.y) * 0.5f),
            Col(col), nm);
        ImGui::PopID();
    }

    // animated accent indicator (position relative to sidebar, move-proof)
    {
        const float target = g_state.activeTab * item_h;
        g_navIndPos =
            (g_navIndPos < 0.0f) ? target : Animate(g_navIndPos, target, 10.0f);
        dl->AddRectFilled(
            ImVec2(org.x, org.y + g_navIndPos),
            ImVec2(org.x + S(3.0f), org.y + g_navIndPos + item_h),
            Col(g_theme.accent), S(4.0f),
            ImDrawFlags_RoundCornersTopRight |
                ImDrawFlags_RoundCornersBottomRight);
    }

    // user profile pinned to the bottom
    {
        const float pad    = S(16.0f);
        const float av     = S(32.0f);
        const float y_row  = ImGui::GetWindowHeight() - pad - av;
        const float y_line = y_row - pad;
        const ImVec2 wpos  = ImGui::GetWindowPos();
        dl->AddLine(ImVec2(wpos.x, wpos.y + y_line),
                    ImVec2(wpos.x + ImGui::GetWindowWidth(), wpos.y + y_line),
                    Col(g_theme.border));
        const ImVec2 a0(org.x + pad, wpos.y + y_row);
        dl->AddRectFilled(a0, a0 + ImVec2(av, av), Col(g_theme.bgInput),
                          S(8.0f));
        {
            const ImVec2 ts = ImGui::CalcTextSize("P");
            dl->AddText(ImVec2(a0.x + (av - ts.x) * 0.5f,
                               a0.y + (av - ts.y) * 0.5f),
                        Col(g_theme.accent), "P");
        }
        const float tx  = a0.x + av + S(10.0f);
        const float fs2 = ImGui::GetFontSize() * 0.85f;
        {
            const ImVec2 ts = ImGui::CalcTextSize("Player");
            const float  h1 = ts.y, h2 = fs2;
            const float  y0 = a0.y + (av - (h1 + h2 + S(2.0f))) * 0.5f;
            dl->AddText(ImVec2(tx, y0), Col(g_theme.textPrimary), "Player");
            dl->AddCircleFilled(ImVec2(tx + S(3.0f), y0 + h1 + S(2.0f) + h2 * 0.5f),
                                S(3.0f), Col(kGreen));
            dl->AddText(ImGui::GetFont(), fs2,
                        ImVec2(tx + S(10.0f), y0 + h1 + S(2.0f)), Col(kGreen),
                        "Online");
        }
    }
}

// ---------------------------------------------------------------------------
// tabs
// ---------------------------------------------------------------------------

bool ContainsNoCase(const char* hay, const char* needle)
{
    if (!needle[0])
        return true;
    for (const char* h = hay; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b && std::tolower((unsigned char)*a) ==
                               std::tolower((unsigned char)*b)) {
            ++a;
            ++b;
        }
        if (!*b)
            return true;
    }
    return false;
}

void EnsureHeroNames(char names[12][16])
{
    if (names[0][0])
        return;
    for (int k = 0; k < 12; ++k)
        std::snprintf(names[k], 16, "Hero %02d", k + 1);
}

void DrawAimTab()
{
    BeginCard("aim-main", "Main");
    ToggleRow("Aim Assist", &g_state.aimAssist);
    ToggleRow("Predict Movement", &g_state.predictMovement);
    ToggleRow("Ignore Obstacles", &g_state.ignoreObstacles);
    EndCard();

    {
        static const char* aim_types[2] = {"Normal", "PSilent"};
        if (Segmented("aim-type", aim_types, 2, &g_state.aimType, true))
            CloseHeroPanel();
        MarginAfter(12.0f);
    }

    if (g_state.aimType == 0) {
        BeginCard("card-normal", "Normal Aim");
        ToggleRow("Enable", &g_state.normal.enable);
        SliderRow("fov-normal-val", "FOV", &g_state.normal.fov, 0, 180,
                  "\xc2\xb0");
        SliderRow("smooth", "Smooth", &g_state.normal.smooth, 0, 100, "%");
        RowLabel("FOV Color");
        SameLineRight(S(4 * 24.0f + 3 * 10.0f));
        Swatches("fov-normal-color", kFovNormalColors, 4,
                 &g_state.normal.colorIdx);
        SliderRow("fov-normal-opacity", "FOV Opacity", &g_state.normal.opacity,
                  0, 100, "%");
        if (ActionButton("Show FOV"))
            g_state.normal.fovShown = !g_state.normal.fovShown;
        EndCard();
    } else {
        BeginCard("card-psilent", "Perfect Silent");
        ToggleRow("Enable", &g_state.psilent.enable);
        SliderRow("fov-psilent-val", "FOV", &g_state.psilent.fov, 0, 180,
                  "\xc2\xb0");
        SliderRow("hit-chance", "Hit Chance", &g_state.psilent.hitChance, 0,
                  100, "%");
        RowLabel("FOV Color");
        SameLineRight(S(4 * 24.0f + 3 * 10.0f));
        Swatches("fov-psilent-color", kFovPsilentColors, 4,
                 &g_state.psilent.colorIdx);
        SliderRow("fov-psilent-opacity", "FOV Opacity",
                  &g_state.psilent.opacity, 0, 100, "%");
        if (ActionButton("Show FOV"))
            g_state.psilent.fovShown = !g_state.psilent.fovShown;
        EndCard();
    }

    BeginCard("targeting", "Targeting");
    {
        static const char* prio[3] = {"Closest", "Health", "Damage"};
        if (Segmented("target-prio", prio, 3, &g_state.targetPrio))
            CloseHeroPanel();
        MarginAfter(12.0f);
    }
    ImGui::TextUnformatted("Aim Points");
    {
        static const char* const pts[6] = {"Head", "Neck", "Chest", "Arms",
                                           "Pelvis", "Legs"};
        for (int i = 0; i < 6; ++i) {
            ImGui::PushID(i);
            if (i > 0)
                ImGui::SameLine(0, S(8.0f));
            Chip(pts[i], &g_state.aimPoints[i]);
            ImGui::PopID();
        }
    }
    MarginAfter(16.0f);
    if (KeybindRow("activation-key", "Activation Key",
                   g_state.activationKey.c_str(),
                   g_capture == CAP_ACTIVATE))
        StartCapture(CAP_ACTIVATE);
    EndCard();

    BeginCard("hs-limit", nullptr);
    {
        const float y0 = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("HS Limit");
        ImGui::PushStyleColor(ImGuiCol_Text, g_theme.textSecondary);
        ImGui::TextUnformatted("Limit headshot percentage");
        ImGui::PopStyleColor();
        const float y_end = ImGui::GetCursorPosY(); // includes trailing spacing

        const float w = ImGui::GetWindowWidth() -
                        ImGui::GetStyle().WindowBorderSize -
                        ImGui::GetStyle().WindowPadding.x - S(36.0f);
        const float ty = y0 + std::max(0.0f, (y_end - y0 - S(20.0f)) * 0.5f);
        ImGui::SetCursorPos(ImVec2(w, ty));
        Toggle("HS Limit", &g_state.hsLimit);
        ImGui::SetCursorPosY(y_end);
    }
    CollapseSliderRow("hs-limit-slider", g_state.hsLimit, "Limit",
                      &g_state.hsLimitVal, 0, 100, "%");
    EndCard();
}

void DrawVisualsTab()
{
    BeginCard("esp", "ESP");
    ToggleRow("ESP", &g_state.esp);
    ToggleRow("Skeleton", &g_state.skeleton);
    ToggleRow("Health Bar", &g_state.healthBar);
    ToggleRow("Name & Dist", &g_state.nameDist);
    ToggleRow("Chams", &g_state.chams);
    EndCard();

    BeginCard("rendering", "Rendering");
    SliderRow("outline-opacity", "Outline Opacity", &g_state.outlineOpacity,
              0, 100, "%");
    SliderRow("draw-distance", "Draw Distance", &g_state.drawDistance, 0, 500,
              "m");
    EndCard();

    BeginCard("colors", "Colors");
    ImGui::TextUnformatted("Allies");
    Swatches("ally-color", kAllyColors, 3, &g_state.allyColor);
    MarginAfter(12.0f);
    ImGui::TextUnformatted("Enemies");
    Swatches("enemy-color", kEnemyColors, 3, &g_state.enemyColor);
    EndCard();
}

void DrawHeroCard(int i, const char* nm)
{
    const float item_h = S(84.0f);

    ImGui::PushID(i);
    const float  w  = std::max(ImGui::GetContentRegionAvail().x, S(76.0f));
    const ImVec2 sz(w, item_h);

    ImGui::InvisibleButton("##hero", sz);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();

    auto*          dl  = ImGui::GetWindowDrawList();
    const ImVec2   r0  = ImGui::GetItemRectMin();
    const ImVec4   bg  = hovered ? g_theme.bgCardHover : g_theme.bgCard;
    const ImVec4   bd  = hovered ? g_theme.borderGlow : g_theme.border;
    dl->AddRectFilled(r0, r0 + sz, Col(bg), S(10.0f));
    dl->AddRect(r0, r0 + sz, Col(bd), S(10.0f), 0, 1.0f);

    const float  av = S(40.0f);
    const ImVec2 a0(r0.x + (w - av) * 0.5f, r0.y + S(12.0f));
    dl->AddRectFilled(a0, a0 + ImVec2(av, av), Col(kHeroColors[i]), S(8.0f));
    {
        const ImVec2 ts = ImGui::CalcTextSize("H");
        dl->AddText(ImVec2(a0.x + (av - ts.x) * 0.5f, a0.y + (av - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 230), "H");
    }
    {
        const float  fs2 = ImGui::GetFontSize() * 0.85f;
        const ImVec2 ts =
            ImGui::GetFont()->CalcTextSizeA(fs2, 10000.0f, 0.0f, nm);
        dl->AddText(ImGui::GetFont(), fs2,
                    ImVec2(r0.x + (w - ts.x) * 0.5f, a0.y + av + S(6.0f)),
                    Col(g_theme.textSecondary), nm);
    }

    if (clicked) {
        g_state.heroOpen     = i;
        g_state.abilityOn[0] = true;
        g_state.abilityOn[1] = true;
        g_state.abilityOn[2] = false;
        g_state.abilityOn[3] = true;
        for (float& f : g_state.abilitySmooth)
            f = 50.0f;
        g_panelRectValid = false;
    }
    ImGui::PopID();
}

void DrawHeroesTab()
{
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##hero-search", "Search hero...",
                             g_state.heroSearch, sizeof(g_state.heroSearch));
    MarginAfter(12.0f);

    static char names[12][16] = {};
    EnsureHeroNames(names);

    const int cols = 6;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(6.0f), S(6.0f)));
    if (ImGui::BeginTable("##heroes", cols,
                          ImGuiTableFlags_SizingStretchSame |
                              ImGuiTableFlags_NoSavedSettings)) {
        int col = 0;
        for (int i = 0; i < 12; ++i) {
            if (!ContainsNoCase(names[i], g_state.heroSearch))
                continue;
            if (col == 0)
                ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawHeroCard(i, names[i]);
            col = (col + 1) % cols;
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void DrawMiscTab()
{
    BeginCard("exploits", "Exploits");
    ToggleRow("Bunny Hop", &g_state.bunnyHop);
    ToggleRow("Auto Accept", &g_state.autoAccept);
    ToggleRow("Auto Active Reload", &g_state.autoActiveReload);
    EndCard();

    BeginCard("view", "View");
    ToggleRow("FOV Changer", &g_state.fovChanger);
    SliderRow("camera-fov", "FOV", &g_state.fovValue, 70, 130, "\xc2\xb0");
    EndCard();
}

void DrawSettingsTab()
{
    BeginCard("settings-iface", "Interface");
    {
        RowLabel("Theme");
        SameLineRight(S(140.0f));
        static const char* themes[2] = {"Dark", "Light"};
        if (Segmented("theme", themes, 2, &g_state.theme, false, 140.0f))
            CloseHeroPanel();
    }
    if (KeybindRow("menu-key", "Menu Key", g_state.menuKey.c_str(),
                   g_capture == CAP_MENU))
        StartCapture(CAP_MENU);
    ToggleRow("UI Animations", &g_state.uiAnimations);
    EndCard();

    BeginCard("settings-scale", "Scaling & Opacity");
    SliderRow("ui-scale", "UI Scale", &g_state.uiScale, 70, 130, "%");
    SliderRow("menu-opacity", "Menu Opacity", &g_state.menuOpacity, 30, 100,
              "%");
    EndCard();

    BeginCard("settings-accent", "Accent Color");
    Swatches("accent-color", kAccentColors, 5, &g_state.accentIdx);
    EndCard();

    if (ActionButton("Reset Settings"))
        ResetSettings();
    if (ActionButton("Save Config"))
        ShowToast("Config saved successfully!");
}

void DrawContent()
{
    switch (g_state.activeTab) {
    case 0: DrawAimTab(); break;
    case 1: DrawVisualsTab(); break;
    case 2: DrawHeroesTab(); break;
    case 3: DrawMiscTab(); break;
    case 4: DrawSettingsTab(); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// hero panel overlay
// ---------------------------------------------------------------------------

void DrawHeroOverlay()
{
    if (g_state.heroOpen < 0) {
        g_panelRectValid = false;
        return;
    }
    const Palette& p  = Pal();
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##hero_overlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 128));

    if (g_panelRectValid) {
        dl->AddRectFilled(g_panelPos + ImVec2(0, S(20.0f)),
                          g_panelPos + g_panelSize + ImVec2(0, S(20.0f)),
                          Col(p.shadow), S(16.0f));
    }

    static char hero_names[12][16] = {};
    EnsureHeroNames(hero_names);

    const float  pw   = S(300.0f);
    const float  ph   = g_panelRectValid ? g_panelSize.y : S(430.0f);
    const ImVec2 ppos((io.DisplaySize.x - pw) * 0.5f,
                      (io.DisplaySize.y - ph) * 0.5f);
    ImGui::SetCursorPos(ppos);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bgMenu);
    ImGui::PushStyleColor(ImGuiCol_Border, p.borderGlow);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(S(16.0f), S(16.0f)));
    ImGui::BeginChild("##hero_panel", ImVec2(pw, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar);

    const ImVec4 tp = p.text, ts = p.textSecondary;
    auto*        cdl = ImGui::GetWindowDrawList();
    const ImVec2 cpos = ImGui::GetWindowPos();
    const float  cb   = ImGui::GetStyle().ChildBorderSize;

    // header: avatar + name + close
    const float av = S(32.0f);
    {
        const ImVec2 top = ImGui::GetCursorScreenPos();
        const ImVec4 col = kHeroColors[g_state.heroOpen];
        cdl->AddRectFilled(top, top + ImVec2(av, av), Col(col), S(8.0f));
        {
            const ImVec2 tsz = ImGui::CalcTextSize("H");
            cdl->AddText(ImVec2(top.x + (av - tsz.x) * 0.5f,
                                top.y + (av - tsz.y) * 0.5f),
                         IM_COL32(255, 255, 255, 230), "H");
        }
        const float tx = top.x + av + S(10.0f);
        cdl->AddText(ImVec2(tx, top.y + S(1.0f)), Col(tp),
                     hero_names[g_state.heroOpen]);
        {
            const float fs2 = ImGui::GetFontSize() * 0.85f;
            cdl->AddText(ImGui::GetFont(), fs2,
                         ImVec2(tx, top.y + ImGui::GetFontSize() + S(6.0f)),
                         Col(ts), "Script Settings");
        }

        const float btn = S(16.0f);
        const float lx  = pw - cb - S(16.0f) - btn;
        const float ly  = (top.y - cpos.y) + (av - btn) * 0.5f;
        ImGui::SetCursorPos(ImVec2(lx, ly));
        ImGui::InvisibleButton("##hp_close", ImVec2(btn, btn));
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        DrawGlyphClose(cdl, mn, mx,
                       ImGui::IsItemHovered() ? Col(tp) : Col(ts), S(1.5f));
        if (ImGui::IsItemClicked())
            CloseHeroPanel();
    }

    // divider (absolute placement: header block = av, gaps 16/16)
    {
        const float y_line = cb + S(16.0f) + av + S(16.0f);
        cdl->AddLine(ImVec2(cpos.x + cb, cpos.y + y_line),
                     ImVec2(cpos.x + pw - cb, cpos.y + y_line), Col(p.border));
        ImGui::SetCursorPosY(y_line + S(16.0f));
    }

    // abilities
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        char label[32];
        if (i == 3)
            std::snprintf(label, sizeof(label), "Ability %d (Ult)", i + 1);
        else
            std::snprintf(label, sizeof(label), "Ability %d", i + 1);

        BeginCard(label, nullptr);
        ToggleRow(label, &g_state.abilityOn[i]);
        char cid[32];
        std::snprintf(cid, sizeof(cid), "ability-smooth-%d", i);
        CollapseSliderRow(cid, g_state.abilityOn[i], "Aim Smoothing",
                          &g_state.abilitySmooth[i], 0, 100, "%");
        EndCard(8.0f);
        ImGui::PopID();
    }

    g_panelPos       = ImGui::GetWindowPos();
    g_panelSize      = ImGui::GetWindowSize();
    g_panelRectValid = true;

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    // click outside the panel closes it
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseHoveringRect(g_panelPos, g_panelPos + g_panelSize))
        CloseHeroPanel();

    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// toasts (foreground, above everything)
// ---------------------------------------------------------------------------

void DrawToasts()
{
    if (g_toasts.empty())
        return;
    const Palette& p   = Pal();
    auto*          dl  = ImGui::GetForegroundDrawList();
    const ImVec2   ds  = ImGui::GetIO().DisplaySize;
    const double   now = ImGui::GetTime();

    // drop expired
    g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(),
                                  [&](const Toast& t) {
                                      return now - t.born > 2.0;
                                  }),
                   g_toasts.end());
    if (g_toasts.empty())
        return;

    float y = ds.y - S(20.0f);
    for (Toast& t : g_toasts) {
        const float age = (float)(now - t.born);
        float       a   = 1.0f;
        if (age < 0.3f)
            a = age / 0.3f;
        else if (age > 1.7f)
            a = std::max(0.0f, (2.0f - age) / 0.3f);
        const float slide =
            (g_animSpeed > 0.0f)
                ? (1.0f - std::min(age / 0.3f, 1.0f)) * S(20.0f)
                : 0.0f;

        const ImVec2 ts = ImGui::CalcTextSize(t.text.c_str());
        const ImVec2 sz(ts.x + S(40.0f), ts.y + S(24.0f));
        const ImVec2 pos(ds.x - S(20.0f) - sz.x, y - sz.y + slide);

        ImVec4 bg = p.bgCardHover;
        bg.w *= a;
        ImVec4 bd = p.borderGlow;
        bd.w *= a;
        ImVec4 tx = p.text;
        tx.w *= a;
        dl->AddRectFilled(pos, pos + sz, Col(bg), S(8.0f));
        dl->AddRect(pos, pos + sz, Col(bd), S(8.0f), 0, 1.0f);
        dl->AddText(pos + ImVec2(S(20.0f), (sz.y - ts.y) * 0.5f), Col(tx),
                    t.text.c_str());
        y -= sz.y + S(8.0f);
    }
}

} // namespace (internal)

// ---------------------------------------------------------------------------
// main entry
// ---------------------------------------------------------------------------

void DrawMenu()
{
    const ImGuiIO& io = ImGui::GetIO();

    g_animSpeed = g_state.uiAnimations ? 1.0f : 0.0f;

    g_openAnim = Animate(g_openAnim, g_state.menuOpen ? 1.0f : 0.0f, 14.0f);
    g_uiScale  = (g_state.uiScale / 100.0f) * (0.95f + 0.05f * g_openAnim);
    const float alpha_factor = (g_state.menuOpacity / 100.0f) * g_openAnim;

    FillWidgetTheme(alpha_factor);
    ApplyStyle(g_uiScale, alpha_factor);

    // input: keybind capture first, then menu toggle key
    UpdateCapture();
    if (g_capture == CAP_NONE) {
        bool toggle = ImGui::IsKeyPressed(ImGuiKey_Insert, false);
        if (!toggle && g_state.menuKeyKey != 0 &&
            g_state.menuKeyKey != ImGuiKey_Insert)
            toggle = ImGui::IsKeyPressed(g_state.menuKeyKey, false);
        if (toggle)
            ToggleMenu();
    }
    if (g_state.menuOpen && !g_wasOpen)
        g_menuPosInit = false;
    g_wasOpen = g_state.menuOpen;

    if (io.DeltaTime > 0.0f)
        g_fps += (1.0f / io.DeltaTime - g_fps) * 0.06f;

    // ---- background --------------------------------------------------------
    DrawFovCircles();

    // ---- menu window -------------------------------------------------------
    if (g_state.menuOpen || g_openAnim > 0.01f) {
        g_heightAnim = Animate(g_heightAnim,
                               g_state.minimized ? 48.0f : 520.0f, 14.0f);

        const float win_w = S(800.0f);
        const float win_h = S(g_heightAnim);

        if (!g_menuPosInit) {
            g_menuPos     = ImVec2((io.DisplaySize.x - win_w) * 0.5f,
                                   (io.DisplaySize.y - win_h) * 0.5f);
            g_menuPosInit = true;
        }
        g_menuPos.x = std::clamp(g_menuPos.x, -win_w + S(80.0f),
                                 io.DisplaySize.x - S(80.0f));
        g_menuPos.y = std::clamp(g_menuPos.y, 0.0f,
                                 std::max(0.0f, io.DisplaySize.y - S(48.0f)));

        // window shadow on the background layer
        {
            auto*          dl = ImGui::GetBackgroundDrawList();
            const ImVec4   sh = Pal().shadow;
            dl->AddRectFilled(g_menuPos + ImVec2(0, S(16.0f)),
                              g_menuPos + ImVec2(win_w, win_h) +
                                  ImVec2(0, S(16.0f)),
                              Col(sh), S(16.0f));
            ImVec4 sh2 = sh;
            sh2.w *= 0.5f;
            dl->AddRectFilled(g_menuPos + ImVec2(S(8.0f), S(28.0f)),
                              g_menuPos + ImVec2(win_w - S(8.0f), win_h) +
                                  ImVec2(0, S(28.0f)),
                              Col(sh2), S(24.0f));
        }

        ImGui::SetNextWindowPos(g_menuPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_Always);
        if (g_refocusMenu) {
            ImGui::SetNextWindowFocus();
            g_refocusMenu = false;
        }

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##nexus_menu", nullptr, flags);
        ImGui::PopStyleVar();

        DrawHeader(win_w);

        if (g_heightAnim > 60.0f) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                ImVec2(0, S(16.0f)));
            ImGui::BeginChild("##sidebar", ImVec2(S(180.0f), 0), 0,
                              ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
            DrawSidebar();
            ImGui::EndChild();
            ImGui::PopStyleVar();

            ImGui::SameLine(0, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                ImVec2(S(20.0f), S(20.0f)));
            ImGui::BeginChild("##content", ImVec2(0, 0), 0,
                              ImGuiWindowFlags_NoScrollbar);
            DrawContent();
            ImGui::EndChild();
            ImGui::PopStyleVar();

            // sidebar divider
            auto*        dl = ImGui::GetWindowDrawList();
            const ImVec2 o  = ImGui::GetWindowPos();
            const float  x  = o.x + ImGui::GetStyle().WindowBorderSize +
                             S(180.0f);
            dl->AddLine(ImVec2(x, o.y + S(48.0f)),
                        ImVec2(x,
                               o.y + win_h - ImGui::GetStyle().WindowBorderSize),
                        Col(g_theme.border));
        }
        ImGui::End();
    }

    // ---- overlays ----------------------------------------------------------
    DrawHeroOverlay();
    DrawToasts();
}

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

State& GetState() { return g_state; }

void ToggleMenu()
{
    g_state.menuOpen = !g_state.menuOpen;
    g_refocusMenu    = true;
}

void ShowToast(const char* msg)
{
    g_toasts.push_back({msg ? msg : "", ImGui::GetTime()});
}

void ResetSettings()
{
    const State d; // defaults

    g_state.normal.fov       = d.normal.fov;
    g_state.normal.smooth    = d.normal.smooth;
    g_state.normal.colorIdx  = d.normal.colorIdx;
    g_state.normal.opacity   = d.normal.opacity;
    g_state.psilent.fov      = d.psilent.fov;
    g_state.psilent.hitChance = d.psilent.hitChance;
    g_state.psilent.colorIdx = d.psilent.colorIdx;
    g_state.psilent.opacity  = d.psilent.opacity;

    g_state.hsLimitVal     = d.hsLimitVal;
    g_state.outlineOpacity = d.outlineOpacity;
    g_state.drawDistance   = d.drawDistance;
    g_state.allyColor      = d.allyColor;
    g_state.enemyColor     = d.enemyColor;
    g_state.fovValue       = d.fovValue;

    g_state.theme       = d.theme;
    g_state.uiScale     = d.uiScale;
    g_state.menuOpacity = d.menuOpacity;
    g_state.accentIdx   = d.accentIdx;

    ShowToast("Settings reset to defaults");
}

ImVec4 GetClearColor() { return Pal().bgApp; }

} // namespace nexus
