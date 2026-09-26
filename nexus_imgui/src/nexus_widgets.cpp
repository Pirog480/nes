#define IMGUI_DEFINE_MATH_OPERATORS
// ============================================================================
//  nexus_widgets.cpp — custom controls ported from uusa.html
// ============================================================================
#include "nexus_widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace nexus {

ThemeValues g_theme   = {};
float g_uiScale       = 1.0f;
float g_animSpeed     = 1.0f;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

float Animate(float current, float target, float speed)
{
    if (g_animSpeed <= 0.0f)
        return target;
    const float dt = ImGui::GetIO().DeltaTime;
    const float k  = 1.0f - std::exp(-speed * g_animSpeed * dt);
    return current + (target - current) * k;
}

static ImU32 Col(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

static ImU32 WithAlpha(const ImVec4& c, float a)
{
    ImVec4 o = c;
    o.w      = a;
    return Col(o);
}

void SameLineRight(float w)
{
    ImGui::SameLine();
    const ImGuiStyle& st   = ImGui::GetStyle();
    const float        winW = ImGui::GetWindowWidth();
    const float right = winW - st.WindowBorderSize - st.WindowPadding.x - w;
    ImGui::SetCursorPosX(right);
}

void RowLabel(const char* label) { ImGui::TextUnformatted(label); }

void TextUpper(const char* title)
{
    char   buf[96];
    size_t i = 0;
    for (; title[i] && i + 1 < sizeof(buf); ++i)
        buf[i] = (char)std::toupper((unsigned char)title[i]);
    buf[i] = '\0';
    ImGui::TextUnformatted(buf);
}

// persistent float in the current window storage, keyed by id
static float GetAnim(ImGuiID id, float def)
{
    return ImGui::GetStateStorage()->GetFloat(id, def);
}
static void SetAnim(ImGuiID id, float v)
{
    ImGui::GetStateStorage()->SetFloat(id, v);
}

// ---------------------------------------------------------------------------
// Toggle — HTML .toggle (36x20, knob 14, travel 16)
// ---------------------------------------------------------------------------

bool Toggle(const char* str_id, bool* v)
{
    const ImGuiID id = ImGui::GetID(str_id);
    const ImVec2 size(S(36.0f), S(20.0f));

    ImGui::InvisibleButton(str_id, size);
    if (ImGui::IsItemClicked())
        *v = !*v;

    const float def = *v ? 1.0f : 0.0f;
    const float cur  = GetAnim(id, def);
    const float t    = Animate(cur, def);
    SetAnim(id, t);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetItemRectMin();

    const float r      = size.y * 0.5f;
    const ImVec4 track = *v ? g_theme.accent : g_theme.bgInput;
    dl->AddRectFilled(p, p + size, Col(track), r);

    // knob
    const float pad = S(2.0f);
    const float kw  = S(14.0f);
    const float kx  = p.x + pad + t * (size.x - pad * 2.0f - kw);
    const ImVec2 kn(kx + kw * 0.5f, p.y + size.y * 0.5f);
    dl->AddCircleFilled(kn + ImVec2(0, S(1.0f)), kw * 0.5f,
                        WithAlpha(ImVec4(0, 0, 0, 1), 0.20f));
    dl->AddCircleFilled(kn, kw * 0.5f, IM_COL32(255, 255, 255, 255));
    return ImGui::IsItemClicked();
}

bool ToggleRow(const char* label, bool* v)
{
    RowLabel(label);
    SameLineRight(S(36.0f));
    return Toggle(label, v);
}

// ---------------------------------------------------------------------------
// SliderRow — label | custom track | value badge (integer values like HTML)
// ---------------------------------------------------------------------------

static bool SliderImpl(const char* str_id, float* v, float v_min, float v_max,
                       const char* unit, bool has_label, const char* label)
{
    const ImGuiStyle& st = ImGui::GetStyle();

    char badge[48];
    std::snprintf(badge, sizeof(badge), "%d%s", (int)std::lround(*v), unit);
    const ImVec2 badge_sz = ImGui::CalcTextSize(badge);
    const float  label_w =
        (has_label && label) ? ImGui::CalcTextSize(label).x : 0.0f;

    const float gap     = S(12.0f);
    const float badge_w = std::max(S(36.0f), badge_sz.x);
    float       avail   = ImGui::GetContentRegionAvail().x;
    float       slider_w =
        avail - label_w - badge_w - (has_label ? gap : 0.0f) - gap;
    slider_w = std::max(slider_w, S(40.0f));

    const float  row_h = S(20.0f);
    const ImVec2 row0  = ImGui::GetCursorPos();

    // label (left)
    if (has_label && label) {
        ImGui::SetCursorPos(row0);
        ImGui::TextUnformatted(label);
    }

    // slider area
    ImGui::SetCursorPos(
        ImVec2(row0.x + label_w + (has_label ? gap : 0.0f), row0.y));
    ImGui::InvisibleButton(str_id, ImVec2(slider_w, row_h));
    // capture geometry immediately — later items overwrite LastItemData
    const ImVec2 btn_min = ImGui::GetItemRectMin();
    bool          changed = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float mx = ImGui::GetIO().MousePos.x - btn_min.x;
        const float t  = std::clamp(mx / std::max(slider_w, 1.0f), 0.0f, 1.0f);
        const float nv = std::round(v_min + t * (v_max - v_min));
        if (nv != *v) {
            *v      = nv;
            changed = true;
        }
    }
    const bool hovered = ImGui::IsItemHovered();

    // badge (right)
    {
        const float bx =
            ImGui::GetWindowWidth() - st.WindowBorderSize - st.WindowPadding.x -
            badge_w;
        ImGui::SetCursorPos(ImVec2(bx, row0.y + (row_h - badge_sz.y) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, g_theme.textSecondary);
        ImGui::TextUnformatted(badge);
        ImGui::PopStyleColor();
    }

    // advance cursor below the row (row_h + standard spacing), draw the track
    ImGui::SetCursorPos(
        ImVec2(row0.x, row0.y + row_h + ImGui::GetStyle().ItemSpacing.y));

    ImDrawList* dl      = ImGui::GetWindowDrawList();
    const ImVec2 smin   = btn_min;
    const ImVec2 smax   = smin + ImVec2(slider_w, row_h);
    const float  frac   = (v_max > v_min) ? ((*v - v_min) / (v_max - v_min))
                                          : 0.0f;
    const float  mid_y  = (smin.y + smax.y) * 0.5f;
    const float  th     = S(4.0f);
    const ImVec2 trk_a(smin.x, mid_y - th * 0.5f);
    const ImVec2 trk_b(smax.x, mid_y + th * 0.5f);

    dl->AddRectFilled(trk_a, trk_b, Col(g_theme.bgInput), th * 0.5f);
    const float fill_x = smin.x + slider_w * frac;
    dl->AddRectFilled(trk_a, ImVec2(fill_x, trk_b.y), Col(g_theme.accent),
                      th * 0.5f);

    const float  kr = S(7.0f) * (hovered ? 1.15f : 1.0f);
    const ImVec2 kn(fill_x, mid_y);
    dl->AddCircleFilled(kn + ImVec2(0, S(1.5f)), kr,
                        WithAlpha(ImVec4(0, 0, 0, 1), 0.25f));
    dl->AddCircleFilled(kn, kr, IM_COL32(255, 255, 255, 255));
    return changed;
}

bool SliderRow(const char* str_id, const char* label, float* v, float v_min,
               float v_max, const char* unit)
{
    return SliderImpl(str_id, v, v_min, v_max, unit, true, label);
}

// ---------------------------------------------------------------------------
// Collapsible slider (single-row content, like the HTML grid-rows collapse)
// ---------------------------------------------------------------------------

bool CollapseSliderRow(const char* str_id, bool open, const char* label,
                       float* v, float v_min, float v_max, const char* unit)
{
    const ImGuiID id  = ImGui::GetID(str_id);
    const float   cur = GetAnim(id, open ? 1.0f : 0.0f);
    const float   t   = Animate(cur, open ? 1.0f : 0.0f, 10.0f);
    SetAnim(id, t);

    const float  full = S(20.0f);
    const float  h    = full * t;
    const ImVec2 start(ImGui::GetCursorScreenPos());

    bool changed = false;
    if (h >= S(1.5f)) {
        ImGui::PushClipRect(start, ImVec2(start.x + 9999.0f, start.y + h),
                            true);
        changed = SliderRow(str_id, label, v, v_min, v_max, unit);
        ImGui::PopClipRect();
    }
    ImGui::SetCursorScreenPos(start + ImVec2(0, h));
    return changed;
}

// ---------------------------------------------------------------------------
// Segmented control
// ---------------------------------------------------------------------------

bool Segmented(const char* str_id, const char* const* items, int count,
               int* current, bool underline, float width)
{
    const float   pad   = S(3.0f);
    const float   btn_h = S(28.0f);
    const float   h     = underline ? btn_h + S(2.0f) : btn_h;

    if (width <= 0.0f)
        width = ImGui::GetContentRegionAvail().x;
    else
        width = S(width);

    ImGui::InvisibleButton(str_id, ImVec2(width, h));
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        const float x = ImGui::GetIO().MousePos.x - ImGui::GetItemRectMin().x;
        int        i  = (int)(x / (width / count));
        i             = std::clamp(i, 0, count - 1);
        if (i != *current) {
            *current = i;
            changed  = true;
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetItemRectMin();

    const float inset = underline ? 0.0f : pad;
    const float slot  = (width - inset * 2.0f) / count;

    // animated indicator position
    const std::string xid_s = std::string(str_id) + "#ind.x";
    const std::string wid_s = std::string(str_id) + "#ind.w";
    const ImGuiID     ix_id = ImGui::GetID(xid_s.c_str());
    const ImGuiID     iw_id = ImGui::GetID(wid_s.c_str());
    const float       tx    = inset + slot * (*current);
    const float ix = Animate(GetAnim(ix_id, tx), tx);
    const float iw = Animate(GetAnim(iw_id, slot), slot);
    SetAnim(ix_id, ix);
    SetAnim(iw_id, iw);

    if (underline) {
        dl->AddLine(p + ImVec2(0, h - S(1.0f)), p + ImVec2(width, h - S(1.0f)),
                    Col(g_theme.border));
        dl->AddRectFilled(p + ImVec2(ix, h - S(2.0f)),
                          p + ImVec2(ix + iw, h), Col(g_theme.accent),
                          S(2.0f));
    } else {
        dl->AddRectFilled(p, p + ImVec2(width, h), Col(g_theme.bgInput),
                          S(8.0f));
        dl->AddRectFilled(p + ImVec2(ix, pad), p + ImVec2(ix + iw, h - pad),
                          Col(g_theme.bgCardHover), S(6.0f));
    }

    // labels (static slots, only the indicator slides)
    ImFont* font = ImGui::GetFont();
    const float      fs   = ImGui::GetFontSize();
    for (int i = 0; i < count; ++i) {
        const ImVec2 ts  = ImGui::CalcTextSize(items[i]);
        const float  cxs = inset + slot * i + slot * 0.5f;
        const ImVec2 tp(p.x + cxs - ts.x * 0.5f,
                        p.y + (h - (underline ? S(2.0f) : 0.0f)) * 0.5f -
                            ts.y * 0.5f);
        dl->AddText(font, fs, tp,
                    Col(i == *current ? g_theme.textPrimary
                                      : g_theme.textSecondary),
                    items[i]);
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Chip
// ---------------------------------------------------------------------------

bool Chip(const char* label, bool* v)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(S(12.0f), S(5.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    const ImVec4 frame = *v ? g_theme.accent : g_theme.bgInput;
    const ImVec4 bordr = *v ? g_theme.accent : g_theme.border;
    const ImVec4 textc = *v ? ImVec4(1, 1, 1, 1) : g_theme.textSecondary;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, frame);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          *v ? g_theme.accent : g_theme.bgCardHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          *v ? g_theme.accent : g_theme.bgCardHover);
    ImGui::PushStyleColor(ImGuiCol_Border, bordr);
    ImGui::PushStyleColor(ImGuiCol_Text, textc);

    const bool pressed = ImGui::Button(label);

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
    if (pressed)
        *v = !*v;
    return pressed;
}

// ---------------------------------------------------------------------------
// Keybind row
// ---------------------------------------------------------------------------

bool KeybindRow(const char* str_id, const char* label, const char* key_text,
                bool listening)
{
    RowLabel(label);
    const ImVec2 ts = ImGui::CalcTextSize(key_text);
    const float  w  = std::max(S(50.0f), ts.x + S(20.0f));
    SameLineRight(w);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(S(10.0f), S(5.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    // listening: accent border with a soft pulse
    float pulse = 1.0f;
    if (listening && g_animSpeed > 0.0f)
        pulse = 0.75f + 0.25f * std::sin((float)ImGui::GetTime() * 6.28f);

    ImVec4 bordr = listening ? g_theme.accent : g_theme.border;
    bordr.w *= listening ? pulse : 1.0f;
    ImVec4 textc = listening ? g_theme.accent : g_theme.textSecondary;
    if (listening)
        textc.w *= pulse;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, g_theme.bgInput);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, g_theme.bgCardHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, g_theme.bgCardHover);
    ImGui::PushStyleColor(ImGuiCol_Border, bordr);
    ImGui::PushStyleColor(ImGuiCol_Text, textc);

    const bool pressed = ImGui::Button(key_text, ImVec2(w, 0));

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
    return pressed;
}

// ---------------------------------------------------------------------------
// Swatches
// ---------------------------------------------------------------------------

bool Swatches(const char* str_id, const ImVec4* colors, int count,
              int* selected, float gap)
{
    bool   changed = false;
    const float d  = S(24.0f);
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        if (i > 0)
            ImGui::SameLine(0, S(gap));
        char id[64];
        std::snprintf(id, sizeof(id), "%s##%d", str_id, i);
        ImGui::InvisibleButton(id, ImVec2(d, d));
        if (ImGui::IsItemClicked()) {
            *selected = i;
            changed   = true;
        }

        ImDrawList* dl   = ImGui::GetWindowDrawList();
        const ImVec2 mn  = ImGui::GetItemRectMin();
        const ImVec2 isz = ImGui::GetItemRectSize();
        const ImVec2 c(mn.x + isz.x * 0.5f, mn.y + isz.y * 0.5f);
        const bool   act = (*selected == i);
        const bool   hov = ImGui::IsItemHovered();
        float        r   = d * 0.5f - S(1.0f);
        if (hov && !act)
            r *= 1.1f;
        dl->AddCircleFilled(c, r, Col(colors[i]));
        if (act) {
            dl->AddCircle(c, d * 0.5f - S(1.0f), Col(g_theme.textPrimary), 0,
                          S(2.0f));
            const float k  = S(4.0f);
            const ImVec2 a(c.x - k, c.y);
            const ImVec2 b(c.x - k * 0.25f, c.y + k * 0.7f);
            const ImVec2 e(c.x + k, c.y - k * 0.7f);
            dl->PathLineTo(a);
            dl->PathLineTo(b);
            dl->PathLineTo(e);
            dl->PathStroke(IM_COL32(255, 255, 255, 255), 0, S(2.0f));
        }
        ImGui::PopID();
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Action button (btn-action)
// ---------------------------------------------------------------------------

bool ActionButton(const char* label)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(S(8.0f), S(8.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, g_theme.bgInput);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, g_theme.bgCardHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, g_theme.bgCardHover);
    ImGui::PushStyleColor(ImGuiCol_Border, g_theme.border);
    ImGui::PushStyleColor(ImGuiCol_Text, g_theme.textPrimary);

    const float w = ImGui::GetContentRegionAvail().x;
    const bool  b = ImGui::Button(label, ImVec2(w, 0));

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
    return b;
}

// ---------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------

bool BeginCard(const char* str_id, const char* title)
{
    ImGui::PushID(str_id);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, g_theme.bgCard);
    ImGui::PushStyleColor(ImGuiCol_Border, g_theme.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(S(16.0f), S(16.0f)));

    ImGui::BeginChild("##card", ImVec2(0, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar);

    // hover highlight (approximates .card:hover background)
    {
        const ImVec2 rp = ImGui::GetWindowPos();
        const ImVec2 rs = ImGui::GetWindowSize();
        if (ImGui::IsMouseHoveringRect(rp, rp + rs)) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                rp + ImVec2(1, 1), rp + rs - ImVec2(1, 1),
                Col(g_theme.bgCardHover), S(11.0f));
        }
    }

    if (title && title[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, g_theme.textSecondary);
        TextUpper(title);
        ImGui::PopStyleColor();
        // title margin-bottom: 12 css px (spacing already added by ItemSize)
        const float auto_g = ImGui::GetStyle().ItemSpacing.y;
        const float want   = S(12.0f);
        if (want > auto_g)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (want - auto_g));
    }
    return true;
}

void EndCard(float bottom_margin_css)
{
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    // card margin-bottom: the standard spacing after the child is already
    // included, so only top up the difference (16 css px by default).
    const float auto_g = ImGui::GetStyle().ItemSpacing.y;
    const float want   = S(bottom_margin_css);
    if (want > auto_g)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (want - auto_g));
}

// ---------------------------------------------------------------------------
// Nav icons (Feather-style strokes redrawn with primitives)
// ---------------------------------------------------------------------------

void DrawNavIcon(ImDrawList* dl, NavIcon icon, ImVec2 pos, float size,
                 ImU32 col)
{
    const float  t = S(1.5f);
    const ImVec2 c(pos.x + size * 0.5f, pos.y + size * 0.5f);

    switch (icon) {
    case NavIcon_Aim: {
        const float r = size * 0.416f;
        dl->AddCircle(c, r, col, 0, t);
        const float a0 = r + size * 0.06f;
        const float a1 = r + size * 0.20f;
        dl->AddLine(ImVec2(c.x + a0, c.y), ImVec2(c.x + a1, c.y), col, t);
        dl->AddLine(ImVec2(c.x - a0, c.y), ImVec2(c.x - a1, c.y), col, t);
        dl->AddLine(ImVec2(c.x, c.y + a0), ImVec2(c.x, c.y + a1), col, t);
        dl->AddLine(ImVec2(c.x, c.y - a0), ImVec2(c.x, c.y - a1), col, t);
        break;
    }
    case NavIcon_Visuals: {
        const float l = pos.x + size * 0.04f;
        const float r = pos.x + size * 0.96f;
        const float m = pos.y + size * 0.5f;
        dl->PathClear();
        dl->PathLineTo(ImVec2(l, m));
        dl->PathBezierCubicCurveTo(ImVec2(l + size * 0.28f, m - size * 0.42f),
                                   ImVec2(r - size * 0.28f, m - size * 0.42f),
                                   ImVec2(r, m));
        dl->PathBezierCubicCurveTo(ImVec2(r - size * 0.28f, m + size * 0.42f),
                                   ImVec2(l + size * 0.28f, m + size * 0.42f),
                                   ImVec2(l, m));
        dl->PathStroke(col, 0, t);
        dl->AddCircle(c, size * 0.14f, col, 0, t);
        break;
    }
    case NavIcon_Heroes: {
        const float  hr = size * 0.17f;
        const ImVec2 h1(pos.x + size * 0.375f, pos.y + size * 0.30f);
        dl->AddCircle(h1, hr, col, 0, t);
        dl->PathClear();
        dl->PathLineTo(ImVec2(pos.x + size * 0.12f, pos.y + size * 0.90f));
        dl->PathBezierCubicCurveTo(
            ImVec2(pos.x + size * 0.14f, pos.y + size * 0.58f),
            ImVec2(pos.x + size * 0.61f, pos.y + size * 0.58f),
            ImVec2(pos.x + size * 0.63f, pos.y + size * 0.90f));
        dl->PathStroke(col, 0, t);
        dl->PathClear();
        dl->PathArcTo(ImVec2(pos.x + size * 0.79f, pos.y + size * 0.32f),
                      hr * 0.9f, -2.2f, 2.0f, 16);
        dl->PathStroke(col, 0, t);
        dl->PathClear();
        dl->PathLineTo(ImVec2(pos.x + size * 0.72f, pos.y + size * 0.66f));
        dl->PathBezierCubicCurveTo(
            ImVec2(pos.x + size * 0.74f, pos.y + size * 0.60f),
            ImVec2(pos.x + size * 1.02f, pos.y + size * 0.60f),
            ImVec2(pos.x + size * 1.02f, pos.y + size * 0.90f));
        dl->PathStroke(col, 0, t);
        break;
    }
    case NavIcon_Misc: {
        dl->AddCircle(c, size * 0.30f, col, 0, t);
        dl->AddCircle(c, size * 0.11f, col, 0, t);
        for (int i = 0; i < 8; ++i) {
            const float ang = (float)i * 6.2831853f / 8.0f;
            const float dx  = std::cos(ang), dy = std::sin(ang);
            dl->AddLine(ImVec2(c.x + dx * size * 0.34f, c.y + dy * size * 0.34f),
                        ImVec2(c.x + dx * size * 0.46f, c.y + dy * size * 0.46f),
                        col, t);
        }
        break;
    }
    case NavIcon_Settings: {
        auto hline = [&](float y, float x0, float x1) {
            dl->AddLine(ImVec2(pos.x + x0 * size, pos.y + y * size),
                        ImVec2(pos.x + x1 * size, pos.y + y * size), col, t);
        };
        auto vline = [&](float x, float y0, float y1) {
            dl->AddLine(ImVec2(pos.x + x * size, pos.y + y0 * size),
                        ImVec2(pos.x + x * size, pos.y + y1 * size), col, t);
        };
        hline(0.583f, 0.04f, 0.30f);
        hline(0.333f, 0.38f, 0.63f);
        hline(0.667f, 0.71f, 0.96f);
        vline(0.17f, 0.13f, 0.87f);
        vline(0.50f, 0.33f, 0.87f);
        vline(0.83f, 0.13f, 0.67f);
        break;
    }
    }
}

} // namespace nexus
