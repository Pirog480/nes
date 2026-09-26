// ============================================================================
//  nexus_widgets.h — custom Dear ImGui widgets ported from the HTML/CSS
//  controls of uusa.html (toggles, sliders with badges, segmented controls,
//  chips, keybinds, color swatches, cards, collapsible sections, nav icons).
//
//  Depends only on <imgui.h>. Requires nexus::g_theme to be filled by the
//  caller (see nexus_menu.h) before drawing.
// ============================================================================
#pragma once

#include <imgui.h>

namespace nexus {

// ---------------------------------------------------------------------------
// Per-frame theme values. Filled every frame by nexus::DrawMenu().
// ---------------------------------------------------------------------------
struct ThemeValues {
    ImVec4 accent;        // accent color (settings swatches)
    ImVec4 textPrimary;
    ImVec4 textSecondary;
    ImVec4 bgInput;
    ImVec4 bgCard;
    ImVec4 bgCardHover;
    ImVec4 border;
    ImVec4 borderGlow;
    ImVec4 shadow;
};
extern ThemeValues g_theme;

// Global UI scale (ui-scale setting * open/close animation factor).
extern float g_uiScale;

// Uniform CSS-pixel -> ImGui-pixel conversion.
inline float S(float v) { return v * g_uiScale; }

// Animation speed multiplier (respects the "UI Animations" setting).
extern float g_animSpeed; // 0 => snap instantly, 1 => smooth

// Exponential smoothing helper used by all animated widgets.
float Animate(float current, float target, float speed = 12.0f);

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

// Right-aligns the next widget of width 'w' inside the current child window.
void SameLineRight(float w);

// Draws 'label' on the left; returns cursor placement helper for the widget.
void RowLabel(const char* label);

// ---------------------------------------------------------------------------
// Controls (each returns true when the value changed)
// ---------------------------------------------------------------------------

// Pill toggle (36x20 css) — HTML: .toggle / .toggle.active
bool Toggle(const char* str_id, bool* v);

// Label + toggle on the right — HTML: .card-row > .label + .toggle
bool ToggleRow(const char* label, bool* v);

// Horizontal slider with left label and right value badge (integer steps).
// HTML: .slider-wrap > .label + input[type=range] + .slider-badge
bool SliderRow(const char* str_id, const char* label, float* v,
               float v_min, float v_max, const char* unit);

// Same as SliderRow but clipped/animated by a collapsible wrapper
// (HTML: .collapsible-wrapper around a .slider-wrap).
bool CollapseSliderRow(const char* str_id, bool open, const char* label,
                       float* v, float v_min, float v_max, const char* unit);

// Segmented control, boxed (default) or underline style.
// HTML: .segmented-ctrl (+ .underline)
bool Segmented(const char* str_id, const char* const* items, int count,
               int* current, bool underline = false, float width = 0.0f);

// Small selectable pill — HTML: .chip / .chip.active
bool Chip(const char* label, bool* v);

// Row with label on the left and a key-capture button on the right.
// 'listening' puts the button into the "waiting for key" state.
bool KeybindRow(const char* str_id, const char* label,
                const char* key_text, bool listening);

// Horizontal row of circular color swatches — HTML: .swatch / .swatch.active
bool Swatches(const char* str_id, const ImVec4* colors, int count, int* selected,
              float gap = 10.0f);

// Full-width "btn-action" button.
bool ActionButton(const char* label);

// ---------------------------------------------------------------------------
// Cards (HTML: .card + .card-title)
// ---------------------------------------------------------------------------
bool BeginCard(const char* str_id, const char* title /* nullptr = untitled */);
void EndCard(float bottom_margin_css = 16.0f);

// ---------------------------------------------------------------------------
// Navigation icons (SVG paths from the HTML, redrawn with primitives)
// ---------------------------------------------------------------------------
enum NavIcon { NavIcon_Aim = 0, NavIcon_Visuals, NavIcon_Heroes, NavIcon_Misc,
               NavIcon_Settings };
void DrawNavIcon(ImDrawList* dl, NavIcon icon, ImVec2 pos, float size,
                 ImU32 col);

// Multi-line-capable uppercase text (HTML titles use text-transform).
void TextUpper(const char* title);

} // namespace nexus
