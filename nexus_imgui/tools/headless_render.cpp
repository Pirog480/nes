#define IMGUI_DEFINE_MATH_OPERATORS
// ============================================================================
//  headless_render.cpp — renders the NEXUS menu without a GPU/window into a
//  PPM image (software rasterizer over ImDrawData). Used to preview/verify
//  the port on machines without OpenGL; also handy for CI screenshots.
//
//  Usage examples:
//      nexus_headless --tab aim     --out aim.ppm
//      nexus_headless --tab heroes --hero 5 --out hero.ppm
//      nexus_headless --tab settings --theme light --out settings_light.ppm
//      nexus_headless --tab aim --fov both --toast --out extras.ppm
//      nexus_headless --tab aim --fov both --closed --out fov.ppm
//      nexus_headless --esp-demo --closed --fov normal --out esp.ppm
// ============================================================================
#include <imgui.h>

#include "../src/esp.h"
#include "../src/game.h"
#include "../src/nexus_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// framebuffer
// ---------------------------------------------------------------------------
struct Framebuffer {
    int w = 0, h = 0;
    std::vector<float> px; // rgba, straight alpha

    void resize(int W, int H)
    {
        w = W;
        h = H;
        px.assign((size_t)w * h * 4, 0.0f);
    }
    void clear(const ImVec4& c)
    {
        for (size_t i = 0; i < px.size(); i += 4) {
            px[i + 0] = c.x;
            px[i + 1] = c.y;
            px[i + 2] = c.z;
            px[i + 3] = 1.0f;
        }
    }
};

static inline void BlendPixel(Framebuffer& fb, int x, int y, float r, float g,
                              float b, float a)
{
    if ((unsigned)x >= (unsigned)fb.w || (unsigned)y >= (unsigned)fb.h)
        return;
    float* p = &fb.px[((size_t)y * fb.w + x) * 4];
    p[0] = r * a + p[0] * (1.0f - a);
    p[1] = g * a + p[1] * (1.0f - a);
    p[2] = b * a + p[2] * (1.0f - a);
    p[3] = a + p[3] * (1.0f - a);
}

// ---------------------------------------------------------------------------
// triangle rasterizer (2x supersampling)
// ---------------------------------------------------------------------------
static void RasterizeTriangles(ImDrawData* dd, const unsigned char* atlas,
                               int atlas_w, int atlas_h, int ss,
                               Framebuffer& out)
{
    const ImVec2 o = dd->DisplayPos;

    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* list = dd->CmdLists[n];
        const ImDrawVert* vtx  = list->VtxBuffer.Data;
        const ImDrawIdx*  idx  = list->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < list->CmdBuffer.Size; ++cmd_i) {
            const ImDrawCmd& cmd = list->CmdBuffer[cmd_i];
            if (cmd.UserCallback)
                continue;

            // clip rect (logical -> supersampled pixels)
            float cx0 = (cmd.ClipRect.x - o.x) * ss;
            float cy0 = (cmd.ClipRect.y - o.y) * ss;
            float cx1 = (cmd.ClipRect.z - o.x) * ss;
            float cy1 = (cmd.ClipRect.w - o.y) * ss;
            cx0 = std::max(cx0, 0.0f);
            cy0 = std::max(cy0, 0.0f);
            cx1 = std::min(cx1, (float)out.w);
            cy1 = std::min(cy1, (float)out.h);
            if (cx1 <= cx0 || cy1 <= cy0)
                continue;

            const int i0_base = (int)cmd.IdxOffset;
            const int v0_base = (int)cmd.VtxOffset;
            for (unsigned int i = 0; i + 2 < cmd.ElemCount; i += 3) {
                const ImDrawVert& a = vtx[v0_base + idx[i0_base + i + 0]];
                const ImDrawVert& b = vtx[v0_base + idx[i0_base + i + 1]];
                const ImDrawVert& c = vtx[v0_base + idx[i0_base + i + 2]];

                const float ax = (a.pos.x - o.x) * ss, ay = (a.pos.y - o.y) * ss;
                const float bx = (b.pos.x - o.x) * ss, by = (b.pos.y - o.y) * ss;
                const float cx = (c.pos.x - o.x) * ss, cy = (c.pos.y - o.y) * ss;

                const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
                if (area == 0.0f)
                    continue;

                const int x0 = std::max((int)std::floor(std::min({ax, bx, cx})),
                                        (int)std::floor(cx0));
                const int x1 = std::min((int)std::ceil(std::max({ax, bx, cx})),
                                        (int)std::ceil(cx1));
                const int y0 = std::max((int)std::floor(std::min({ay, by, cy})),
                                        (int)std::floor(cy0));
                const int y1 = std::min((int)std::ceil(std::max({ay, by, cy})),
                                        (int)std::ceil(cy1));
                if (x1 <= x0 || y1 <= y0)
                    continue;

                // unpack vertex color (IM_COL32 = r|g<<8|b<<16|a<<24)
                const ImU32 col = a.col;
                const float vr  = (col & 0xFF) / 255.0f;
                const float vg  = ((col >> 8) & 0xFF) / 255.0f;
                const float vb  = ((col >> 16) & 0xFF) / 255.0f;
                const float va  = ((col >> 24) & 0xFF) / 255.0f;

                for (int y = y0; y < y1; ++y) {
                    const float py = y + 0.5f;
                    for (int x = x0; x < x1; ++x) {
                        const float px = x + 0.5f;
                        // proper barycentric coordinates:
                        //   w0 -> vertex A, w1 -> B, w2 -> C
                        const float w0 = (bx - px) * (cy - py) -
                                         (by - py) * (cx - px);
                        const float w1 = (cx - px) * (ay - py) -
                                         (cy - py) * (ax - px);
                        const float w2 = (ax - px) * (by - py) -
                                         (ay - py) * (bx - px);
                        // same-sign test covers both windings
                        if (w0 * area < 0.0f || w1 * area < 0.0f ||
                            w2 * area < 0.0f)
                            continue;

                        const float t0 = w0 / area;
                        const float t1 = w1 / area;
                        const float t2 = w2 / area;

                        // interpolate uv and sample the font atlas
                        const float u = t0 * a.uv.x + t1 * b.uv.x + t2 * c.uv.x;
                        const float v = t0 * a.uv.y + t1 * b.uv.y + t2 * c.uv.y;
                        int tx = (int)(u * atlas_w);
                        int ty = (int)(v * atlas_h);
                        tx = std::clamp(tx, 0, atlas_w - 1);
                        ty = std::clamp(ty, 0, atlas_h - 1);
                        const unsigned char* tex =
                            atlas + ((size_t)ty * atlas_w + tx) * 4;
                        const float ta = tex[3] / 255.0f;

                        const float sa = va * ta;
                        if (sa <= 0.0f)
                            continue;
                        // atlas RGBA32: RGB are white everywhere, coverage in
                        // alpha; the vertex color provides the tint.
                        BlendPixel(out, x, y, vr, vg, vb, sa);
                    }
                }
            }
        }
    }
}

static bool WritePPM(const Framebuffer& fb, const char* path, int ss)
{
    FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    std::fprintf(f, "P6\n%d %d\n255\n", fb.w / ss, fb.h / ss);
    std::vector<unsigned char> row((size_t)(fb.w / ss) * 3);
    for (int y = 0; y < fb.h / ss; ++y) {
        for (int x = 0; x < fb.w / ss; ++x) {
            float r = 0, g = 0, b = 0;
            for (int sy = 0; sy < ss; ++sy)
                for (int sx = 0; sx < ss; ++sx) {
                    const float* p =
                        &fb.px[((size_t)(y * ss + sy) * fb.w + x * ss + sx) * 4];
                    r += p[0];
                    g += p[1];
                    b += p[2];
                }
            const float inv = 1.0f / (float)(ss * ss);
            row[x * 3 + 0] = (unsigned char)std::clamp(r * inv * 255.0f, 0.0f, 255.0f);
            row[x * 3 + 1] = (unsigned char)std::clamp(g * inv * 255.0f, 0.0f, 255.0f);
            row[x * 3 + 2] = (unsigned char)std::clamp(b * inv * 255.0f, 0.0f, 255.0f);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// --esp-demo: a synthetic snapshot (no game, no backend) so the ESP can be
// screenshotted headlessly. Camera at (0,0,64) looking along +X with a 90 deg
// fov; two enemies and one ally, all with valid synthetic bones.
// ---------------------------------------------------------------------------
static void FillDemoBones(game::PlayerSnap& p)
{
    // a small humanoid, relative to the feet (origin)
    static const float dy[game::BoneCount] = {0, 0, 0, 0, 0, -9, 9, -5, 5};
    static const float dz[game::BoneCount] = {36, 46, 54, 62, 68, 54, 54, 18, 18};
    for (int i = 0; i < game::BoneCount; ++i) {
        p.bone[i]      = p.origin + game::Vec3(0, dy[i], dz[i]);
        p.boneValid[i] = true;
    }
    game::ValidateBones(p); // must keep all nine
}

static game::PlayerSnap MakeDemoPlayer(const char* name, float x, float y,
                                       int team, bool enemy, int hp, int hpMax)
{
    game::PlayerSnap p;
    p.valid       = true;
    p.alive       = true;
    p.isLocal     = false;
    p.isEnemy     = enemy;
    p.team        = team;
    p.health      = hp;
    p.maxHealth   = hpMax;
    p.origin      = game::Vec3(x, y, 0);
    p.velocity    = game::Vec3(0, 0, 0);
    p.viewOffsetZ = 64.0f;
    std::snprintf(p.name, sizeof(p.name), "%s", name);
    FillDemoBones(p);
    return p;
}

static game::Snapshot MakeEspDemoSnapshot(int width, int height)
{
    game::Snapshot s;
    s.Clear();
    s.valid = true;

    s.camera.valid  = true;
    s.camera.pos    = game::Vec3(0, 0, 64);
    s.camera.pitch  = 0.0f;
    s.camera.yaw    = 0.0f;
    s.camera.fovX   = 90.0f;
    s.camera.width  = width;
    s.camera.height = height;

    s.weaponInReload = false;

    // left half of the screen, right half, and one ally
    s.players[0] = MakeDemoPlayer("Enemy One", 600, -460, 3, true, 187, 260);
    s.players[1] = MakeDemoPlayer("Enemy Two", 1400, 1072, 3, true, 84, 240);
    s.players[2] = MakeDemoPlayer("Ally", 700, 656, 2, false, 240, 240);
    s.count      = 3;
    return s;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // options
    int   width = 1280, height = 720;
    int   tab   = 0;
    bool  light = false;
    int   hero  = -1;
    bool  fovN = false, fovP = false;
    bool  toast = false;
    bool  minimized = false;
    bool  closed = false;
    bool  espDemo = false;
    int   frames = 220;
    const char* out = "screenshot.ppm";

    for (int i = 1; i < argc; ++i) {
        auto is = [&](const char* s) { return !std::strcmp(argv[i], s); };
        if (is("--out") && i + 1 < argc) out = argv[++i];
        else if (is("--size") && i + 1 < argc)
            std::sscanf(argv[++i], "%dx%d", &width, &height);
        else if (is("--tab") && i + 1 < argc) {
            const char* t = argv[++i];
            if (!std::strcmp(t, "aim")) tab = 0;
            else if (!std::strcmp(t, "visuals")) tab = 1;
            else if (!std::strcmp(t, "heroes")) tab = 2;
            else if (!std::strcmp(t, "misc")) tab = 3;
            else if (!std::strcmp(t, "settings")) tab = 4;
            else tab = std::atoi(t);
        } else if (is("--theme") && i + 1 < argc)
            light = !std::strcmp(argv[++i], "light");
        else if (is("--hero") && i + 1 < argc)
            hero = std::atoi(argv[++i]);
        else if (is("--fov") && i + 1 < argc) {
            // "normal" => only the normal-aim circle, "psilent" => only the
            // psilent one, "both" => both. (This used to be inverted.)
            const char* v = argv[++i];
            fovN = !std::strcmp(v, "normal") || !std::strcmp(v, "both");
            fovP = !std::strcmp(v, "psilent") || !std::strcmp(v, "both");
        } else if (is("--toast"))
            toast = true;
        else if (is("--min"))
            minimized = true;
        else if (is("--closed"))
            closed = true;
        else if (is("--esp-demo"))
            espDemo = true;
        else if (is("--frames") && i + 1 < argc)
            frames = std::atoi(argv[++i]);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime   = 1.0f / 60.0f;

    // font: prefer DejaVu for a closer look to a real app font
    ImFont* font = io.Fonts->AddFontFromFileTTF(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15.0f);
    if (!font)
        io.Fonts->AddFontDefault();

    unsigned char* atlas = nullptr;
    int atlas_w = 0, atlas_h = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &atlas_w, &atlas_h);

    // preset state
    nexus::State& st = nexus::GetState();
    st.activeTab = tab;
    st.theme     = light ? 1 : 0;
    st.minimized = minimized;
    st.menuOpen  = !closed;
    if (hero >= 0 && hero < 12) {
        st.heroOpen     = hero;
        st.activeTab    = 2;
        st.abilityOn[0] = true;
        st.abilityOn[1] = true;
        st.abilityOn[2] = false;
        st.abilityOn[3] = true;
    }
    if (fovN)
        st.normal.fovShown = true;
    if (fovP)
        st.psilent.fovShown = true;
    st.normal.fovShown = st.normal.fovShown || fovN;
    st.psilent.fovShown = st.psilent.fovShown || fovP;

    // ESP demo: Visuals tab with every ESP toggle on + a synthetic snapshot
    game::Snapshot snap;
    if (espDemo) {
        st.activeTab = 1; // Visuals
        st.esp       = true;
        st.skeleton  = true;
        st.healthBar = true;
        st.nameDist  = true;
        snap         = MakeEspDemoSnapshot(width, height);
    }

    // frames (let animations settle, then optionally show a toast)
    for (int f = 0; f < frames; ++f) {
        io.AddMousePosEvent(-10000.0f, -10000.0f);
        ImGui::NewFrame();
        if (toast && f == frames - 25)
            nexus::ShowToast("Config saved successfully!");
        nexus::DrawMenu();
        // ESP goes on the background draw list AFTER the menu painted it, so it
        // ends up above the background/FOV circles but below the menu window.
        if (espDemo)
            nexus_esp::Render(snap);
        ImGui::EndFrame();
    }
    ImGui::Render();

    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || dd->CmdListsCount == 0) {
        fprintf(stderr, "nothing to render\n");
        return 1;
    }

    const int ss = 2;
    Framebuffer fb;
    fb.resize(width * ss, height * ss);
    fb.clear(nexus::GetClearColor());
    RasterizeTriangles(dd, atlas, atlas_w, atlas_h, ss, fb);

    if (!WritePPM(fb, out, ss)) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("wrote %s (%dx%d, %d cmd lists)\n", out, width, height,
           dd->CmdListsCount);

    ImGui::DestroyContext();
    return 0;
}
