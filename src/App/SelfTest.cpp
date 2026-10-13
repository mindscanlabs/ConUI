#include "SelfTest.hpp"
#include "Scene.hpp"
#ifdef CONUI_PLAYGROUND
#ifdef CONUI_ABOUT_CAT
#include "AboutCat.hpp"
#endif
#include "Playground.hpp"
#include "ThemeEditor.hpp"
#endif
#include "../Core/ResourceManager.hpp"
#include "../Core/RasterStore.hpp"
#include "../Core/Utils.hpp"
#include "../Render/CellBuffer.hpp"
#include "../Render/FontAtlas.hpp"
#include "../Render/GLLoader.hpp"
#include "../Render/Renderer.hpp"
#include "../UI/Glyphs.hpp"
#include "../UI/Theme.hpp"
#include "../UI/WindowChrome.hpp"
#include "../UI/Strings.hpp"
#include "../UI/Context.hpp"
#include "../UI/Layout.hpp"
#include "../UI/Widgets/Widgets.hpp"
#include "../Core/Json.hpp"
#include "../Core/ThemeStore.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_fail;
}

std::vector<uint8_t> loadFontBytes() {
    size_t n = 0;
    const void* p = ResourceManager::Get().GetResource("JetBrainsMono-Regular.ttf", n);
    if (p && n) return std::vector<uint8_t>((const uint8_t*)p, (const uint8_t*)p + n);
    return {};
}

} // namespace

namespace {

/**
 * Snapshot of a screen: its cells as text, against a baseline kept in the
 * repository (tests/snapshots/). A layout regression then shows up as a diff
 * of rows, with no screenshots involved. build.sh says where the
 * baselines are (CONUI_SNAPSHOT_DIR); without it the comparison is skipped,
 * so a copy in dist/ still runs every other check. Meant the change? Run the
 * build with CONUI_SNAPSHOT_UPDATE=1 and the baselines are rewritten.
 */
std::string snapDir() {
    const char* d = std::getenv("CONUI_SNAPSHOT_DIR");
    if (!d || !*d) return "";
    std::string s = d;
    if (s.back() != '/' && s.back() != '\\') s += '/';
    return s;
}

void checkSnapshot(const std::string& name, const std::string& shot, const std::string& outDir) {
    const std::string base = snapDir();
    if (base.empty()) return;                       // no baselines here: nothing to compare
    const std::string path = base + name + ".txt";
    if (std::getenv("CONUI_SNAPSHOT_UPDATE")) {
        std::filesystem::create_directories(base);
        std::ofstream(path, std::ios::binary) << shot;
        std::printf("      updated %s.txt\n", name.c_str());
        return;
    }
    std::ifstream f(path, std::ios::binary);
    char msg[128];
    if (!f) {
        std::snprintf(msg, sizeof msg, "snapshot %s has a baseline", name.c_str());
        std::printf("      no baseline: %s (run with CONUI_SNAPSHOT_UPDATE=1)\n", path.c_str());
        check(false, msg);
        return;
    }
    const std::string want((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::snprintf(msg, sizeof msg, "snapshot %s matches its baseline", name.c_str());
    if (want == shot) { check(true, msg); return; }
    // Say WHICH row moved, and leave the new one next to the build for a look.
    size_t a = 0, b = 0;
    int row = 1;
    while (a < want.size() && b < shot.size()) {
        const size_t ea = want.find('\n', a), eb = shot.find('\n', b);
        if (want.compare(a, ea - a, shot, b, eb - b) != 0) {
            std::printf("      row %d differs\n        was: %s\n        now: %s\n", row,
                        want.substr(a, ea - a).c_str(), shot.substr(b, eb - b).c_str());
            break;
        }
        a = ea + 1; b = eb + 1; ++row;
    }
    std::ofstream(outDir + name + ".actual.txt", std::ios::binary) << shot;
    check(false, msg);
}

} // namespace

int RunSelfTest(const char* outDir) {
    g_fail = 0;
    const std::string dir = outDir ? outDir : "";
    std::printf("ConUI self-test\n");

    ResourceManager::Get().Load(dir + "resources.dat");
    std::vector<uint8_t> ttf = loadFontBytes();
    check(!ttf.empty(), "font found in resources.dat");
    if (ttf.empty()) return 1;

    // ── 1. font coverage ────────────────────────────────────────────────────
    {
        FontAtlas probe;
        probe.init(ttf, 9, 20);
        int missing = 0;
        for (int i = 0; i < Gly::kAllCount; ++i)
            if (!probe.hasGlyph(Gly::kAll[i])) {
                std::printf("      missing U+%04X\n", Gly::kAll[i]);
                ++missing;
            }
        char msg[96];
        std::snprintf(msg, sizeof msg, "font covers all %d vocabulary glyphs", Gly::kAllCount);
        check(missing == 0, msg);

        char m2[96];
        std::snprintf(m2, sizeof m2, "cell geometry: advance %d, lineHeight %d, ascent %d",
                      probe.advanceUnits(), probe.lineHeightUnits(), probe.ascentUnits());
        check(probe.advanceUnits() == 600 && probe.lineHeightUnits() == 1320, m2);
    }

    // ── 1b. caption icons: every size is in resources.dat and splits ───────
    // The caption boxes are raster masks from resources (§8.1). A sheet that
    // is missing or has the wrong shape would silently fall back to geometry
    // at that size — so every size the app can ask for is checked here.
    {
        int bad = 0;
        RasterStore rs;
        rs.init(1024, 1024);
        for (int s = 8; s <= 32; ++s) {
            size_t n = 0;
            const std::string name = "caption_" + std::to_string(s) + ".png";
            const void* p = ResourceManager::Get().GetResource(name, n);
            chrome::CaptionIcons ic;
            const int px = p ? rs.addStripFromMemory((const uint8_t*)p, n, chrome::BTN_COUNT + 1, ic.id) : 0;
            ic.px = px;
            bool square = ic.valid();
            for (int i = 0; square && i <= chrome::BTN_COUNT; ++i) {
                const RasterInfo& ri = rs.get((uint16_t)ic.id[i]);
                square = ri.aw == s && ri.ah == s;
            }
            if (!square) { std::printf("      %s missing or not 5 × %dpx\n", name.c_str(), s); ++bad; }
        }
        check(bad == 0, "caption icon sheets 8..32 px are packed and split into 5 square icons");

        // The sidebar's section icons, the same way: one strip per size the
        // app can ask for (a row tall: 12..48 px), five icons each.
        int badSec = 0;
        for (int s = 12; s <= 48; ++s) {
            size_t n = 0;
            const std::string name = "sections_" + std::to_string(s) + ".png";
            const void* p = ResourceManager::Get().GetResource(name, n);
            int ids[5] = { -1, -1, -1, -1, -1 };
            const int px = p ? rs.addStripFromMemory((const uint8_t*)p, n, 5, ids) : 0;
            if (px != s) { std::printf("      %s missing or not 5 × %dpx\n", name.c_str(), s); ++badSec; }
        }
        check(badSec == 0, "sidebar section icon strips 12..48 px are packed, 5 icons each");

        // With icons, the boxes are sprites tinted with theme slots — close in
        // danger — and no geometry is drawn for them.
        CellBuffer cb;
        cb.resize(40, 3, BG_WINDOW);
        chrome::CaptionIcons ic;
        size_t n = 0;
        const void* p = ResourceManager::Get().GetResource("caption_11.png", n);
        if (p) ic.px = rs.addStripFromMemory((const uint8_t*)p, n, chrome::BTN_COUNT + 1, ic.id);
        chrome::DrawCaption(cb, "T", true, 9, 20, chrome::BTN_NONE, chrome::BTN_NONE, 0, &ic);
        bool tinted = cb.sprites().size() == (size_t)chrome::BTN_COUNT;
        for (const SpriteRef& sp : cb.sprites()) tinted = tinted && sp.tint < SLOT_COUNT;
        tinted = tinted && !cb.sprites().empty() && cb.sprites()[0].tint == DANGER;
        // the band is the only shape left: no box geometry, and no rule under it
        check(ic.valid() && tinted && cb.shapes().size() == 1,
              "caption boxes are tinted raster icons, close in danger, no box geometry, no rule");

        // The hit test follows the BOX the pointer sees, at every cell
        // size — not the band around it, which is a third of a row taller and
        // used to light a box up with the pointer well above or below it.
        int offBox = 0, missed = 0;
        for (Density d : { Density::Tiny, Density::Compact, Density::Normal, Density::Large }) {
            const float fp = FontPxForDensity(d);
            const int cw = (int)std::lround(0.600f * fp), ch = (int)std::lround(1.320f * fp);
            const int box = chrome::CaptionBoxPx(ch), band = chrome::CaptionHeightPx(ch);
            const int top = (band - box) / 2;
            chrome::ButtonSpan sp[chrome::BTN_COUNT];
            chrome::ButtonSpans(40, cw, ch, sp);
            for (int b = 0; b < chrome::BTN_COUNT; ++b) {
                const int mid = (sp[b].x0 + sp[b].x1) / 2;
                if (chrome::HitTest(mid, top + box / 2, 40, cw, ch) != b) ++missed;
                if (chrome::HitTest(mid, 0, 40, cw, ch) != chrome::BTN_NONE) ++offBox;
                if (chrome::HitTest(mid, band - 1, 40, cw, ch) != chrome::BTN_NONE) ++offBox;
            }
        }
        check(missed == 0 && offBox == 0,
              "caption boxes: the hit test is the box, not the band, at every density");
        if (missed || offBox) std::printf("      %d missed, %d off the box\n", missed, offBox);
    }

    // ── 2. built-ins match themes/*.json ────────────────────────────────────
    {
        int mismatches = 0;
        for (int i = 0; i < BuiltinThemeCount(); ++i) {
            const Theme& t = BuiltinTheme(i);
            std::string lower;
            for (char c : t.name) lower += (char)std::tolower((unsigned char)c);
            std::ifstream f(dir + "themes/" + lower + ".json", std::ios::binary);
            if (!f) { std::printf("      themes/%s.json not found\n", lower.c_str()); ++mismatches; continue; }
            std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            for (int s = 0; s < SLOT_COUNT; ++s) {
                std::string want = FormatHexColor(t.c[s]);
                if (src.find("\"" + want + "\"") == std::string::npos) {
                    std::printf("      %s: %s = %s not in file\n",
                                lower.c_str(), kSlotNames[s], want.c_str());
                    ++mismatches;
                }
            }
        }
        check(mismatches == 0, "built-in themes agree with themes/*.json");
    }

    // ── 3. no seams in a solid field, at every supported cell size ─────────
    // This is the property the whole rasterisation scheme exists for
    // (DESIGN_SYSTEM §3), and it breaks the moment someone rounds the wrong
    // value — so it gets checked at every size, not just the default.
    // The property that matters is NOT "the █ bitmap is exactly one cell" —
    // subpixel positioning legitimately spills it into a 21st row that carries
    // no coverage, and the offset compensates. What matters is that a FIELD of
    // █ leaves no hole. So tile it 3×3 the way the renderer does, blend the
    // same way the renderer blends, and inspect the centre cell — every pixel
    // there sees the seams with all four neighbours.
    {
        int bad_sizes = 0;
        for (int fontPx = 11; fontPx <= 24; ++fontPx) {
            const int cw = (int)std::lround(0.600f * fontPx);
            const int chh = (int)std::lround(1.320f * fontPx);
            FontAtlas fa;
            if (!fa.init(ttf, cw, chh)) { ++bad_sizes; continue; }
            const GlyphInfo& g = fa.glyph(Gly::FULL);
            if (g.blank) {
                std::printf("      font_px=%d cell=%dx%d -> █ has no bitmap\n", fontPx, cw, chh);
                ++bad_sizes;
                continue;
            }

            const int FW = cw * 3, FH = chh * 3;
            std::vector<float> field((size_t)FW * FH, 0.0f);
            for (int cy = 0; cy < 3; ++cy)
                for (int cx = 0; cx < 3; ++cx)
                    for (int j = 0; j < g.ah; ++j)
                        for (int i = 0; i < g.aw; ++i) {
                            const int px = cx * cw + g.dx + i;
                            const int py = cy * chh + g.dy + j;
                            if (px < 0 || py < 0 || px >= FW || py >= FH) continue;
                            const float cov =
                                fa.pixels()[(size_t)(g.ay + j) * fa.texW() + g.ax + i] / 255.0f;
                            float& dst = field[(size_t)py * FW + px];
                            dst = dst + cov * (1.0f - dst);   // source-over
                        }

            float worst = 1.0f;
            int wx = 0, wy = 0;
            for (int j = chh; j < chh * 2; ++j)
                for (int i = cw; i < cw * 2; ++i) {
                    const float v = field[(size_t)j * FW + i];
                    if (v < worst) { worst = v; wx = i - cw; wy = j - chh; }
                }
            // 254/255 — anything below that is a visible seam, anything above
            // is the blend arithmetic's last bit.
            if (worst < 254.0f / 255.0f) {
                std::printf("      font_px=%d cell=%dx%d -> seam at (%d,%d), coverage %.3f"
                            " [glyph %dx%d at (%d,%d)]\n",
                            fontPx, cw, chh, wx, wy, worst, g.aw, g.ah, g.dx, g.dy);
                ++bad_sizes;
            }
        }
        check(bad_sizes == 0, "solid block tiles seamlessly at font_px 11..24");
    }

    // Frame lines are drawn, not rasterized: one cell, whole pixels, and the
    // strokes of │ ├ ┼ (and of ─ ┬ ┼) on the very same pixels, so a vertical
    // frame has no overlap to band at its joints and every junction closes.
    {
        int bad = 0;
        for (int fontPx = 11; fontPx <= 24; ++fontPx) {
            const int cw = (int)std::lround(0.600f * fontPx);
            const int chh = (int)std::lround(1.320f * fontPx);
            FontAtlas fa;
            if (!fa.init(ttf, cw, chh)) { ++bad; continue; }
            // Coverage of glyph `cp` at (x, y) in its cell, or -1 if it is not
            // exactly one cell of pure 0/255.
            auto cov = [&](uint32_t cp, int x, int y) -> int {
                const GlyphInfo& g = fa.glyph(cp);
                if (g.blank || g.aw != cw || g.ah != chh || g.dx != 0 || g.dy != 0) return -1;
                return fa.pixels()[(size_t)(g.ay + y) * fa.texW() + g.ax + x];
            };
            for (uint32_t cp : { Gly::H, Gly::V, Gly::TL, Gly::TR, Gly::BL, Gly::BR, Gly::TEE_R,
                                 Gly::TEE_L, Gly::TEE_D, Gly::TEE_U, Gly::CROSS })
                for (int y = 0; y < chh; ++y)
                    for (int x = 0; x < cw; ++x) {
                        const int c = cov(cp, x, y);
                        if (c != 0 && c != 255) { ++bad; y = chh; break; }
                    }
            // The vertical stroke: the columns │ covers on the top row, the
            // same for ├ and ┼; the horizontal: the rows ─ covers, the same
            // for ┬ and ┼ at the right edge.
            for (int x = 0; x < cw; ++x)
                if (cov(Gly::V, x, 0) != cov(Gly::TEE_R, x, 0) || cov(Gly::V, x, 0) != cov(Gly::CROSS, x, chh - 1)) ++bad;
            for (int y = 0; y < chh; ++y)
                if (cov(Gly::H, 0, y) != cov(Gly::TEE_D, 0, y) || cov(Gly::H, cw - 1, y) != cov(Gly::CROSS, cw - 1, y)) ++bad;
            if (cov(Gly::V, cw / 2, 0) != 255 || cov(Gly::V, cw / 2, chh - 1) != 255) ++bad;   // edge to edge
        }
        check(bad == 0, "frame lines are one cell of whole pixels and meet at every joint (font_px 11..24)");
    }

    // ── 4. cell grid round-trips through dump() ─────────────────────────────
    {
        CellBuffer cb;
        cb.resize(20, 3, BG_WINDOW);
        cb.text(0, 0, "┌──────────────────┐", BORDER, BG_WINDOW);
        cb.text(0, 1, "│ Привет, ConUI ██ │", TEXT, BG_WINDOW);
        cb.text(0, 2, "└──────────────────┘", BORDER, BG_WINDOW);
        const std::string want =
            "┌──────────────────┐\n"
            "│ Привет, ConUI ██ │\n"
            "└──────────────────┘\n";
        check(cb.dump() == want, "CellBuffer::dump() round-trips UTF-8");
        if (cb.dump() != want) std::printf("      got:\n%s", cb.dump().c_str());
    }

    // ── 5. render one real frame off-screen ─────────────────────────────────
    {
        CellBuffer cells;
        const Theme& theme = BuiltinTheme(0); // Phosphor
        RasterStore rasters;
        rasters.init();
        BuildSelfTestScene(cells, MakeDemoRaster(rasters));

        FontAtlas atlas;
        atlas.init(ttf, 9, 20);
        for (int y = 0; y < cells.height(); ++y)
            for (int x = 0; x < cells.width(); ++x) atlas.glyph(cells.at(x, y).cp);

        const int W = cells.width() * atlas.cellW();
        const int H = cells.height() * atlas.cellH();

        GLuint fbo = 0, colour = 0;
        gl::GenFramebuffers(1, &fbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &colour);
        glBindTexture(GL_TEXTURE_2D, colour);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
        bool fboOk = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        check(fboOk, "offscreen framebuffer complete");

        if (fboOk) {
            Renderer r;
            check(r.init(), "renderer compiles and links");

            const Rgba& bg = theme.c[BG_WINDOW];
            glViewport(0, 0, W, H);
            glClearColor(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            r.draw(cells, atlas, theme, W, H, 0, 0, &rasters, nullptr, 1.0f,
                   chrome::CaptionTextPadPx(atlas.cellH()));

            std::vector<uint8_t> rgba((size_t)W * H * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

            // flip (GL origin is bottom-left) and drop alpha
            std::vector<uint8_t> rgb((size_t)W * H * 3);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const uint8_t* s = &rgba[((size_t)(H - 1 - y) * W + x) * 4];
                    uint8_t* d = &rgb[((size_t)y * W + x) * 3];
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                }

            // Something must actually have been drawn: at least one pixel
            // differs from the clear colour.
            size_t drawn = 0;
            for (size_t i = 0; i < rgb.size(); i += 3)
                if (rgb[i] != bg.r || rgb[i + 1] != bg.g || rgb[i + 2] != bg.b) ++drawn;
            char msg[96];
            std::snprintf(msg, sizeof msg, "frame is not blank (%zu/%d pixels painted)",
                          drawn, W * H);
            check(drawn > (size_t)(W * H) / 100, msg);

            std::printf("  frame: %dx%d px, grid %dx%d, cell %dx%d\n",
                        W, H, cells.width(), cells.height(), atlas.cellW(), atlas.cellH());

            // ── the live window body, at the size a real window gets ────────
            // Same renderer, same atlas; the only difference is that this
            // scene does not own its grid, so it also exercises the "any size
            // the framebuffer happens to divide into" path.
            {
                CellBuffer live;
                live.resize(96, 34, BG_WINDOW);
                chrome::DrawCaption(live, "CONUI · PLAYGROUND", true,
                                    atlas.cellW(), atlas.cellH());
                BuildStageScene(live, chrome::CONTENT_TOP, 0, /*compact=*/false);
                for (int y = 0; y < live.height(); ++y)
                    for (int x = 0; x < live.width(); ++x) atlas.glyph(live.at(x, y).cp);

                const int LW = live.width() * atlas.cellW();
                const int LH = live.height() * atlas.cellH();
                // The FBO fits the largest screen rendered below — the
                // playground at 124 × 41. Smaller ones use its bottom-left corner.
                const int FW = std::max(LW, 124 * atlas.cellW());
                const int FH = std::max(LH, 41 * atlas.cellH());
                GLuint lfbo = 0, lcol = 0;
                gl::GenFramebuffers(1, &lfbo);
                gl::BindFramebuffer(GL_FRAMEBUFFER, lfbo);
                glGenTextures(1, &lcol);
                glBindTexture(GL_TEXTURE_2D, lcol);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FW, FH, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         GL_TEXTURE_2D, lcol, 0);
                if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                    glViewport(0, 0, LW, LH);
                    glClearColor(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                    r.draw(live, atlas, theme, LW, LH, 0, 0, &rasters, nullptr, 1.0f,
                           chrome::CaptionTextPadPx(atlas.cellH()));

                    std::vector<uint8_t> lrgba((size_t)LW * LH * 4);
                    glReadPixels(0, 0, LW, LH, GL_RGBA, GL_UNSIGNED_BYTE, lrgba.data());
                    size_t painted = 0;
                    for (size_t i = 0; i < lrgba.size(); i += 4)
                        if (lrgba[i] != bg.r || lrgba[i + 1] != bg.g || lrgba[i + 2] != bg.b) ++painted;
                    check(painted > (size_t)(LW * LH) / 100, "the live window body is not blank");
                }

#ifdef CONUI_PLAYGROUND
                // The playground, once in each locale — the strongest check on
                // §14 there is: the same code, the same layout, two languages,
                // and nothing overflowing its column. Every screen and every
                // overlay is rendered and compared as text with its baseline.
                for (int loc = 0; loc < str::LOCALE_COUNT; ++loc) {
                    str::SetLocale(loc);
                    CellBuffer lb;
                    lb.resize(124, 41, BG_WINDOW);                // the playground's default grid
                    ui::Context ctx;
                    ui::Layout lay;
                    PlaygroundState pg;
                    pg.frozen = true;          // a snapshot must not depend on the clock
                    ThemeStore ts;
                    ts.init("");
                    AppState st;
                    auto frame = [&](const ui::Input& in) {
                        lb.clear(BG_WINDOW);
                        chrome::DrawCaption(lb, str::T(str::ID_APP_TITLE), true,
                                            atlas.cellW(), atlas.cellH());
                        AppServices s{ ts, st, lay, rasters, 0, 60.0, 12408, false };
                        AppActions a;
                        ctx.begin(lb, in);
                        BuildPlayground(ctx, s, a, pg, 0);
                        ctx.end();
                    };
                    auto at2 = [](float x, float y) { ui::Input i; i.mouse_x = x; i.mouse_y = y; return i; };
                    // Renders a buffer and compares its text dump with the baseline (the same
                    // frame as cells: a layout regression is a diff).
                    auto shootBuf = [&](const char* tag, CellBuffer& sb) {
                        const int SW = sb.width() * atlas.cellW(), SH = sb.height() * atlas.cellH();
                        // top(): what is ON SCREEN, overlay grid and dimming
                        // included — the menus' glyphs would otherwise never
                        // reach the atlas.
                        for (int y = 0; y < sb.height(); ++y)
                            for (int x = 0; x < sb.width(); ++x) atlas.glyph(sb.top(x, y).cp);
                        glViewport(0, 0, SW, SH);
                        glClearColor(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f, 1.0f);
                        glClear(GL_COLOR_BUFFER_BIT);
                        r.draw(sb, atlas, theme, SW, SH, 0, 0, &rasters, nullptr, 1.0f,
                               chrome::CaptionTextPadPx(atlas.cellH()));
                        const std::string name = std::string(tag) + "-" + str::LocaleCode(loc);
                        checkSnapshot(name, sb.dump(), dir);
                    };
                    auto shoot = [&](const char* tag) { shootBuf(tag, lb); };

                    // Workspace. The first frame measures the scroll regions
                    // (content height is known only at the END of a frame), the
                    // last one hovers a button.
                    frame(ui::Input{});
                    frame(ui::Input{});
                    frame(at2(8.5f, 6.5f));
                    shoot("stage6-workspace");

                    // A menu of the bar: press VIEW (" FILE " is x 1..6, then
                    // one cell, " VIEW " is x 8..13 on row 2).
                    {
                        ui::Input p = at2(10.5f, 2.5f); p.mouse_down = p.mouse_pressed = true;
                        frame(p);
                        ui::Input rl = at2(10.5f, 2.5f); rl.mouse_released = true;
                        frame(rl);
                        frame(at2(10.5f, 2.5f));
                        frame(at2(10.5f, 2.5f));
                        shoot("stage6-menubar");
                        ui::Input esc = at2(10.5f, 2.5f); esc.key_escape = true;
                        frame(esc);
                        frame(ui::Input{});
                    }

                    // The context menu on the file tree, walked down to "Sort
                    // by" and opened — menu and submenu on one picture.
                    {
                        ui::Input rp = at2(52.5f, 10.5f);
                        rp.mouse_right_pressed = true;
                        frame(rp);
                        const ui::Input still = at2(52.5f, 10.5f);
                        frame(still);
                        for (int k = 0; k < 5; ++k) {
                            ui::Input d = still; d.key_down = true; frame(d);
                        }
                        ui::Input rt = still; rt.key_right = true; frame(rt);
                        frame(still);
                        frame(still);
                        shoot("stage6-menu");
                        ui::Input esc = still; esc.key_escape = true;
                        frame(esc);
                        frame(esc);
                        frame(ui::Input{});
                    }

                    // The modal, over everything, the window dimmed beneath.
                    pg.confirm = true;
                    frame(ui::Input{});
                    frame(ui::Input{});
                    shoot("stage6-modal");
                    pg.confirm = false;

                    // ABOUT, with the cat frozen in its three-quarter view, or the
                    // text alone in a build without the cat: its own baseline.
                    pg.about = true;
                    frame(ui::Input{});
                    frame(ui::Input{});
#ifdef CONUI_ABOUT_CAT
                    shoot("stage6-about");
#else
                    shoot("stage6-about-nocat");
#endif
                    pg.about = false;

                    // The other screens (Charts move with the clock: no baseline).
                    pg.screen = 1;
                    frame(ui::Input{});
                    frame(ui::Input{});
                    shoot("stage6-states");
                    pg.screen = 2;
                    frame(ui::Input{});
                    frame(ui::Input{});
                    shoot("stage6-glyphs");
                    pg.screen = 4;
                    frame(ui::Input{});
                    frame(ui::Input{});
                    shoot("stage6-diagrams");
                    pg.screen = 0;

                    // The theme editor — the second window's whole body (§8.16),
                    // 60 × 34 as specified, one slot changed so "modified" shows.
                    {
                        CellBuffer eb;
                        eb.resize(60, 34, BG_WINDOW);
                        ThemeStore es_store;
                        es_store.init("");
                        es_store.edit(0).c[BG_PANEL] = Rgba{ 0x0E, 0x38, 0x62, 0xFF };
                        ThemeEditorState es;
                        ui::Layout el;
                        ui::Context ec;
                        for (int k = 0; k < 2; ++k) {
                            eb.clear(BG_WINDOW);
                            chrome::DrawCaption(eb, str::T(str::ID_THEME_EDITOR), true,
                                                atlas.cellW(), atlas.cellH());
                            ec.begin(eb, ui::Input{});
                            BuildThemeEditor(ec, el, es, es_store, 0, chrome::CONTENT_TOP);
                            ec.end();
                        }
                        shootBuf("stage6-editor", eb);
                    }
                }
#endif
                str::SetLocale(str::LOCALE_EN);
                glDeleteTextures(1, &lcol);
                gl::DeleteFramebuffers(1, &lfbo);
                gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            // ── the layout must survive absurd sizes ────────────
            {
                int survived = 0;
                const int sizes[][2] = { {10,4}, {24,6}, {40,12}, {200,60} };
                for (const auto& s : sizes) {
                    CellBuffer tiny;
                    tiny.resize(s[0], s[1], BG_WINDOW);
                    chrome::DrawCaption(tiny, "T", true, atlas.cellW(), atlas.cellH());
                    BuildStageScene(tiny, chrome::CONTENT_TOP, -1, false);
                    BuildStageScene(tiny, chrome::CONTENT_TOP, -1, true);
                    ++survived;   // reaching here without an out-of-range write is the test
                }
                check(survived == 4, "body layout copes with 10x4 .. 200x60");
            }

            // ── clipping reaches the pixels ─────────────────────
            // A pill across the whole buffer, drawn inside a 4 x 2 cell clip.
            // No pixel of it may land outside the clip, and the clip edge must
            // be cut STRAIGHT — the corners of the clip rectangle are fully
            // covered, which they would not be if the SDF were computed from
            // the trimmed quad instead of from the whole shape.
            {
                CellBuffer cc;
                cc.resize(10, 4, BG_WINDOW);
                cc.pushClip(2, 1, 4, 2);
                cc.shape(0.0f, 0.0f, 10.0f, 4.0f, ACCENT, 0.5f);
                cc.popClip();
                const int CW = 10 * atlas.cellW(), CH = 4 * atlas.cellH();
                GLuint cfbo = 0, ccol = 0;
                gl::GenFramebuffers(1, &cfbo);
                gl::BindFramebuffer(GL_FRAMEBUFFER, cfbo);
                glGenTextures(1, &ccol);
                glBindTexture(GL_TEXTURE_2D, ccol);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CW, CH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ccol, 0);
                glViewport(0, 0, CW, CH);
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                Theme t = theme;
                t.c[BG_WINDOW] = Rgba{ 0, 0, 0, 0 };          // nothing but the shape
                t.c[ACCENT] = Rgba{ 255, 255, 255, 255 };
                r.draw(cc, atlas, t, CW, CH, 0, 0, nullptr);
                std::vector<uint8_t> px((size_t)CW * CH * 4);
                glReadPixels(0, 0, CW, CH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                const int x0 = 2 * atlas.cellW(), x1 = 6 * atlas.cellW();
                const int y0 = 1 * atlas.cellH(), y1 = 3 * atlas.cellH();
                int outside = 0, inside_missing = 0;
                for (int y = 0; y < CH; ++y)
                    for (int x = 0; x < CW; ++x) {
                        const uint8_t v = px[((size_t)(CH - 1 - y) * CW + x) * 4];
                        const bool in = x >= x0 && x < x1 && y >= y0 && y < y1;
                        if (!in && v != 0) ++outside;
                        if (in && v != 255) ++inside_missing;
                    }
                check(outside == 0 && inside_missing == 0,
                      "a clipped shape stays inside its clip and is cut straight");
                if (outside || inside_missing)
                    std::printf("      %d px outside, %d px inside not covered\n",
                                outside, inside_missing);
                glDeleteTextures(1, &ccol);
                gl::DeleteFramebuffers(1, &cfbo);
                gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            // ── the layers ───────────────────────────────────────
            // dump() sees cells only, so geometry and raster need their own
            // eyes: these render a small scene and read the pixels back.
            {
                // Renders `cb` with `t` and hands back the pixels, top-left first.
                auto shotOf = [&](CellBuffer& cb, const Theme& t, float panelAlpha,
                                  RasterStore* rs) {
                    const int LW = cb.width() * atlas.cellW(), LH = cb.height() * atlas.cellH();
                    for (int y = 0; y < cb.height(); ++y)
                        for (int x = 0; x < cb.width(); ++x) atlas.glyph(cb.top(x, y).cp);
                    GLuint f = 0, c = 0;
                    gl::GenFramebuffers(1, &f);
                    gl::BindFramebuffer(GL_FRAMEBUFFER, f);
                    glGenTextures(1, &c);
                    glBindTexture(GL_TEXTURE_2D, c);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LW, LH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c, 0);
                    glViewport(0, 0, LW, LH);
                    glClearColor(0, 0, 0, 1);
                    glClear(GL_COLOR_BUFFER_BIT);
                    r.draw(cb, atlas, t, LW, LH, 0, 0, rs, nullptr, 1.0f, 0.0f, panelAlpha);
                    std::vector<uint8_t> px((size_t)LW * LH * 4);
                    glReadPixels(0, 0, LW, LH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                    glDeleteTextures(1, &c);
                    gl::DeleteFramebuffers(1, &f);
                    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
                    std::vector<uint8_t> flipped((size_t)LW * LH * 4);
                    for (int y = 0; y < LH; ++y)
                        std::memcpy(&flipped[(size_t)y * LW * 4],
                                    &px[(size_t)(LH - 1 - y) * LW * 4], (size_t)LW * 4);
                    return flipped;
                };

                Theme lt = theme;                       // colours that can be told apart
                lt.c[BG_WINDOW] = Rgba{ 100, 100, 100, 255 };
                lt.c[BG_PANEL]  = Rgba{ 0, 0, 255, 255 };
                lt.c[ACCENT]    = Rgba{ 255, 0, 0, 255 };

                // Order: a scrollbar thumb lives in the shape list, a modal in
                // the overlay band. Not one red pixel may land inside the modal.
                {
                    CellBuffer cb;
                    cb.resize(20, 8, BG_WINDOW);
                    cb.shape(10.2f, 1.0f, 0.5f, 6.0f, ACCENT, 0.5f);     // the thumb
                    cb.setBand(Z_OVERLAY);
                    ui::DrawFrame(cb, ui::Rect{ 8, 2, 8, 4 }, BORDER, BG_PANEL);
                    cb.setBand(Z_CONTENT);
                    const std::vector<uint8_t> px = shotOf(cb, lt, 1.0f, nullptr);
                    const int LW = cb.width() * atlas.cellW();
                    int through = 0;
                    // Inside the modal, past its own frame glyphs: rows 3..4, cols 9..14.
                    for (int y = 3 * atlas.cellH(); y < 5 * atlas.cellH(); ++y)
                        for (int x = 9 * atlas.cellW(); x < 15 * atlas.cellW(); ++x) {
                            const uint8_t* p = &px[((size_t)y * LW + x) * 4];
                            if (p[0] > 128 && p[2] < 128) ++through;     // red, not blue
                        }
                    check(through == 0, "layers: a shape never paints through an overlay above it");
                    if (through) std::printf("      %d px of the thumb inside the modal\n", through);
                }

                // A popup's ground stops at its frame line (§8.1), and the
                // window carries on underneath: the half cell outside the line
                // shows the window's own cell background, not bare ground. The
                // modal tint (§8.9) sits between the two — it dims that half
                // cell and leaves the card at full strength.
                {
                    Theme dt = lt;
                    dt.c[SHADOW] = Rgba{ 0, 0, 0, 128 };
                    CellBuffer cb;
                    cb.resize(20, 8, BG_WINDOW);
                    cb.bgRect(0, 0, 20, 8, ACCENT);                  // the window under it
                    cb.setDimBase(true);
                    cb.setBand(Z_OVERLAY);
                    ui::DrawFrame(cb, ui::Rect{ 8, 2, 8, 4 }, BORDER, BG_PANEL);
                    cb.setBand(Z_CONTENT);
                    const std::vector<uint8_t> px = shotOf(cb, dt, 1.0f, nullptr);
                    const int LW = cb.width() * atlas.cellW();
                    auto at = [&](int x, int y) { return &px[((size_t)y * LW + x) * 4]; };
                    // The top pixel row of a top-edge cell: above the frame line,
                    // between the ─ glyph and the cell's edge.
                    const uint8_t* out = at(11 * atlas.cellW() + atlas.cellW() / 2,
                                            2 * atlas.cellH());
                    const uint8_t* in2 = at(11 * atlas.cellW() + atlas.cellW() / 2,
                                            3 * atlas.cellH() + atlas.cellH() / 2);
                    const bool through2 = out[0] > 60 && out[0] < 200 && out[2] < 60;  // dimmed red
                    const bool card = in2[2] > 200 && in2[0] < 60;                     // full blue
                    check(through2, "layers: the window shows through outside a popup's frame line");
                    check(card, "layers: the modal tint dims the window, not the modal");
                    if (!through2) std::printf("      outside the line: %d %d %d\n", out[0], out[1], out[2]);
                    if (!card)     std::printf("      inside the card:  %d %d %d\n", in2[0], in2[1], in2[2]);
                }

                // Double alpha: a cell whose background IS the panel's layer
                // colour must not paint it a second time — at 50 % that would
                // darken the middle of every panel.
                {
                    CellBuffer cb;
                    cb.resize(12, 5, BG_WINDOW);
                    ui::DrawFrame(cb, ui::Rect{ 0, 0, 12, 5 }, BORDER, BG_PANEL);
                    const std::vector<uint8_t> px = shotOf(cb, lt, 0.5f, nullptr);
                    const int LW = cb.width() * atlas.cellW();
                    const uint8_t* p = &px[((size_t)(2 * atlas.cellH() + 5) * LW +
                                            (6 * atlas.cellW() + 4)) * 4];
                    // window 100 under panel 0,0,255 at 50 %: one blend, not two.
                    const int wantR = 50, wantB = (100 + 255) / 2;
                    const bool once = std::abs((int)p[0] - wantR) <= 2 && std::abs((int)p[2] - wantB) <= 2;
                    check(once, "layers: a panel's ground is blended once, not once per cell");
                    if (!once) std::printf("      got %d,%d,%d, expected %d,%d,%d\n",
                                           p[0], p[1], p[2], wantR, wantR, wantB);
                }

                // Raster: a sprite fills its box and nothing outside it.
                {
                    CellBuffer cb;
                    cb.resize(10, 4, BG_WINDOW);
                    cb.sprite(2.0f, 1.0f, 4.0f, 2.0f, (uint16_t)MakeDemoRaster(rasters));
                    Theme bt = lt;
                    bt.c[BG_WINDOW] = Rgba{ 0, 0, 0, 255 };
                    const std::vector<uint8_t> px = shotOf(cb, bt, 1.0f, &rasters);
                    const int LW = cb.width() * atlas.cellW();
                    auto lit = [&](int x, int y) {
                        const uint8_t* p = &px[((size_t)y * LW + x) * 4];
                        return p[0] + p[1] + p[2] > 12;
                    };
                    const int bx0 = 2 * atlas.cellW(), bx1 = 6 * atlas.cellW();
                    const int by0 = 1 * atlas.cellH(), by1 = 3 * atlas.cellH();
                    const bool inside = lit(bx0 + 1, by0 + 1) && lit(bx1 - 2, by1 - 2);
                    const bool outside = !lit(bx0 - 2, by0 + 1) && !lit(bx1 + 1, by1 - 2) &&
                                         !lit(bx0 + 1, by0 - 2) && !lit(bx1 - 2, by1 + 1);
                    check(inside && outside, "layers: a raster element fills its box and no more");
                }
            }
            r.shutdown();
        }
        glDeleteTextures(1, &colour);
        gl::DeleteFramebuffers(1, &fbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ── 6. the JSON parser, on input that is wrong ──────────────────────────
    // This is the code most likely to be subtly broken, so it is tested on
    // BAD input first and good input second. Every case must produce an error
    // with a line number, never a crash and never a half-parsed document.
    {
        struct Bad { const char* src; int line; const char* what; };
        const Bad bad[] = {
            { "{\n  \"a\": 1,\n",                    3, "truncated file" },
            { "{\n  \"a\": \"oops\n}",               2, "unterminated string" },
            { "{\n  \"a\": 1,\n  \"a\": 2\n}",       3, "duplicate key" },
            { "{\n  \"a\": \x01\x02\n}",             2, "garbage bytes" },
            { "{ \"a\": 01 }",                       1, "leading zero rejected" },
            { "{ \"a\": \"\\q\" }",                  1, "unknown escape" },
            { "{ \"a\": \"\\uD800\" }",              1, "lone surrogate" },
            { "{ \"a\": 1 } trailing",               1, "trailing characters" },
            { "{ \"a\" 1 }",                         1, "missing colon" },
            { "// hi\n{}",                           1, "comments rejected" },
        };
        int wrong = 0;
        for (const Bad& b : bad) {
            json::Value v;
            json::Error e;
            const bool ok = json::Parse(b.src, v, e);
            if (ok || e.line == 0) {
                std::printf("      %s: expected an error, got %s\n",
                            b.what, ok ? "success" : "a blank error");
                ++wrong;
            } else if (e.line != b.line) {
                std::printf("      %s: reported line %d, expected %d (%s)\n",
                            b.what, e.line, b.line, e.message.c_str());
                ++wrong;
            }
        }
        check(wrong == 0, "JSON rejects bad input with a line number");

        // Deep nesting must be refused, not recursed into.
        {
            std::string deep;
            for (int i = 0; i < 64; ++i) deep += "{\"a\":";
            deep += "1";
            for (int i = 0; i < 64; ++i) deep += "}";
            json::Value v; json::Error e;
            check(!json::Parse(deep, v, e) && e.line > 0, "JSON refuses deep nesting");
        }

        // Escapes and UTF-8 survive a round trip unchanged.
        {
            const std::string src =
                "{\"s\":\"a\\\"b\\\\c\\n\\u0416\\uD83D\\uDE00\",\"n\":-2.5,\"i\":7,"
                "\"t\":true,\"z\":null,\"arr\":[1,2]}";
            json::Value v; json::Error e;
            const bool ok = json::Parse(src, v, e);
            std::string s;
            const bool got = ok && v.getString("s", s);
            const bool esc = got && s == "a\"b\\c\n\xD0\x96\xF0\x9F\x98\x80";
            json::Value v2; json::Error e2;
            const bool again = ok && json::Parse(json::Write(v), v2, e2);
            std::string s2;
            const bool same = again && v2.getString("s", s2) && s2 == s &&
                              json::Write(v) == json::Write(v2);
            if (!esc) std::printf("      escapes decoded to: %s\n", s.c_str());
            check(ok && esc && same, "JSON round-trips escapes, UTF-8 and numbers");
        }
    }

    // ── 7. themes as files ──────────────────────────────────────────────────
    {
        // Round trip: every built-in must survive write -> read -> write.
        int broken = 0;
        for (int i = 0; i < BuiltinThemeCount(); ++i) {
            const Theme& src = BuiltinTheme(i);
            const std::string text = ThemeStore::writeTheme(src);
            Theme back;
            std::string err;
            if (!ThemeStore::parseTheme(text, src, back, err)) {
                std::printf("      %s: %s\n", src.name.c_str(), err.c_str());
                ++broken;
                continue;
            }
            for (int s = 0; s < SLOT_COUNT; ++s) {
                const Rgba& a = src.c[s];
                const Rgba& b = back.c[s];
                if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) {
                    std::printf("      %s: %s changed on round trip\n",
                                src.name.c_str(), kSlotNames[s]);
                    ++broken;
                }
            }
            if (ThemeStore::writeTheme(back) != text) {
                std::printf("      %s: second write differs\n", src.name.c_str());
                ++broken;
            }
        }
        check(broken == 0, "built-in themes round-trip through themes/*.json");

        // A file with ONE colour in it is valid; everything else falls back.
        {
            const Theme& base = BuiltinTheme(0);
            const std::string one =
                "{\"name\":\"Phosphor\",\"colors\":{\"accent\":\"#FF00FF\"}}";
            Theme t; std::string err;
            const bool ok = ThemeStore::parseTheme(one, base, t, err);
            const bool overridden = ok && t.c[ACCENT].r == 0xFF && t.c[ACCENT].g == 0x00 &&
                                    t.c[ACCENT].b == 0xFF;
            bool rest_kept = ok;
            for (int s = 0; s < SLOT_COUNT && rest_kept; ++s) {
                if (s == ACCENT) continue;
                const Rgba& a = base.c[s];
                const Rgba& b = t.c[s];
                rest_kept = (a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a);
            }
            if (!ok) std::printf("      %s\n", err.c_str());
            check(ok && overridden && rest_kept, "a one-slot theme file is valid");
        }

        // Things that must be refused, each with something to show a human.
        {
            const Theme& base = BuiltinTheme(0);
            struct BadTheme { const char* src; const char* what; };
            const BadTheme bad[] = {
                { "{\"colors\":{}}",                                   "no name" },
                { "{\"name\":\"X\"}",                                  "no colors" },
                { "{\"name\":\"X\",\"colors\":{\"accent\":\"red\"}}",  "colour not hex" },
                { "{\"name\":\"X\",\"colors\":{\"accent\":7}}",        "colour not a string" },
                { "{\"name\":\"X\",\"colors\":[]}",                    "colors not an object" },
            };
            int accepted = 0;
            for (const BadTheme& b : bad) {
                Theme t; std::string err;
                if (ThemeStore::parseTheme(b.src, base, t, err) || err.empty()) {
                    std::printf("      %s: accepted or gave no message\n", b.what);
                    ++accepted;
                }
            }
            check(accepted == 0, "broken theme files are refused with a reason");
        }

        // An unknown key is forward compatibility, not an error.
        {
            const Theme& base = BuiltinTheme(0);
            Theme t; std::string err;
            const bool ok = ThemeStore::parseTheme(
                "{\"name\":\"X\",\"colors\":{\"accent\":\"#112233\","
                "\"slot.from.the.future\":\"#445566\"}}", base, t, err);
            check(ok, "an unknown colour key is ignored, not fatal");
        }

        // The live store: the built-ins must be there, and themes/*.json on
        // disk must agree with them.
        {
            ThemeStore store;
            store.init(dir);
            bool all_present = store.count() >= BuiltinThemeCount();
            for (int i = 0; i < BuiltinThemeCount() && all_present; ++i)
                all_present = store.indexOf(BuiltinTheme(i).name.c_str()) >= 0;
            for (const std::string& w : store.warnings())
                std::printf("      warning: %s\n", w.c_str());
            check(all_present && store.warnings().empty(),
                  "ThemeStore loads themes/ with no warnings");
        }
    }

    // ── 8. the localisation layer ───────────────────────────────────────────
    {
        // Every string has an English entry, and no translation may exceed the
        // width its column can afford — a layout that only breaks in Russian
        // is a layout that breaks in front of a user (§14).
        int over = 0, missing = 0;
        for (int i = 0; i < str::ID_COUNT; ++i) {
            const str::Id id = (str::Id)i;
            const char* en = str::T(id, str::LOCALE_EN);
            if (!en || !*en) { std::printf("      %s: no English\n", str::Key(id)); ++missing; }
            const int limit = str::MaxWidth(id);
            if (limit <= 0) continue;
            for (int loc = 0; loc < str::LOCALE_COUNT; ++loc) {
                // A multi-line string (a tooltip) is measured line by line: the
                // budget is what one row of the grid can hold.
                int w = 0, run = 0;
                for (uint32_t cp : Utf8Decode(str::T(id, loc))) {
                    if (cp == '\n') { run = 0; continue; }
                    w = std::max(w, ++run);
                }
                if (w > limit) {
                    std::printf("      %s [%s]: %d cells, limit %d\n",
                                str::Key(id), str::LocaleCode(loc), w, limit);
                    ++over;
                }
            }
        }
        check(missing == 0, "every string has an English entry");
        check(over == 0, "no translation exceeds its declared width");

        // Switching locale actually changes what T() returns, and switching
        // back restores it.
        str::SetLocale(str::LOCALE_RU);
        const bool ru = std::strcmp(str::T(str::ID_RUN), "ЗАПУСК") == 0;
        str::SetLocale(str::LOCALE_EN);
        const bool en = std::strcmp(str::T(str::ID_RUN), "RUN") == 0;
        check(ru && en, "T() follows the current locale");
    }

    // ── 9. the immediate-mode core ──────────────────────────────────────────
    {
        CellBuffer cb;
        cb.resize(40, 6, BG_WINDOW);
        ui::Context ctx;

        // Identity: the same label in two scopes must be two different ids.
        ctx.pushId("panelA");
        const ui::Id a = ctx.makeId("RUN");
        ctx.popId();
        ctx.pushId("panelB");
        const ui::Id b = ctx.makeId("RUN");
        ctx.popId();
        check(a != b && a != ui::kNoId && b != ui::kNoId,
              "the same label in two scopes gets two ids");

        // A click: press and release over the button.
        ui::Input in;
        in.mouse_x = 3.5f; in.mouse_y = 0.5f;

        in.mouse_down = true; in.mouse_pressed = true;
        ctx.begin(cb, in);
        bool fired_on_press = ui::Button(ctx, "RUN");
        ctx.end();

        in.mouse_pressed = false; in.mouse_down = false; in.mouse_released = true;
        ctx.begin(cb, in);
        const bool fired_on_release = ui::Button(ctx, "RUN");
        ctx.end();
        check(!fired_on_press && fired_on_release, "a button fires on release, not on press");

        // Releasing somewhere else must NOT fire.
        ui::Context c2;
        ui::Input in2;
        in2.mouse_x = 3.5f; in2.mouse_y = 0.5f;
        in2.mouse_down = true; in2.mouse_pressed = true;
        c2.begin(cb, in2); ui::Button(c2, "RUN"); c2.end();
        in2.mouse_x = 30.0f;                       // slid off the button
        in2.mouse_pressed = false; in2.mouse_down = false; in2.mouse_released = true;
        c2.begin(cb, in2);
        const bool fired_away = ui::Button(c2, "RUN");
        c2.end();
        check(!fired_away, "releasing off the button does not fire it");

        // Keyboard: Tab focuses, Enter activates — the same result as a click.
        ui::Context c3;
        ui::Input k;
        k.key_tab = true;
        c3.begin(cb, k); ui::Button(c3, "ONE"); ui::Button(c3, "TWO"); c3.end();
        const bool focused_first = c3.focusedId() != ui::kNoId;

        k.key_tab = false; k.key_enter = true;
        c3.begin(cb, k);
        const bool one = ui::Button(c3, "ONE");
        const bool two = ui::Button(c3, "TWO");
        c3.end();
        check(focused_first && one && !two, "Tab focuses and Enter activates it");

        // Layout: regions clip, and allocation walks the cursor.
        ui::Context c4;
        ui::Input none;
        c4.begin(cb, none);
        c4.pushRegion(ui::Rect{ 2, 1, 10, 3 });
        const ui::Rect r1 = c4.allocate(4, 1);
        const ui::Rect r2 = c4.allocate(4, 1);
        c4.newline();
        const ui::Rect r3 = c4.allocate(4, 1);
        // A region larger than its parent is clipped to it, never beyond.
        c4.pushRegion(ui::Rect{ 0, 0, 999, 999 });
        const ui::Rect inner = c4.region();
        c4.popRegion();
        c4.popRegion();
        c4.end();
        check(r1.x == 2 && r2.x == 6 && r3.x == 2 && r3.y == 2 &&
              inner.x == 2 && inner.w == 10 && inner.h == 3,
              "layout allocates in cells and regions clip to their parent");
    }

    // ── 10. widgets ───────────────────────────────────────────────
    // Behaviour, not looks: the looks are in the screen snapshots. Every case drives
    // the widget through frames exactly as the live loop would.
    {
        // Clipping in the cell buffer: every writer respects it.
        {
            CellBuffer cb;
            cb.resize(20, 6, BG_WINDOW);
            cb.pushClip(2, 1, 5, 2);
            cb.put(0, 0, 'X', TEXT, BG_WINDOW);
            cb.put(3, 1, 'Y', TEXT, BG_WINDOW);
            cb.shape(10.0f, 4.0f, 2.0f, 1.0f, ACCENT);          // wholly outside
            cb.shape(0.0f, 0.0f, 20.0f, 6.0f, ACCENT);          // crosses it
            cb.surface(0, 0, 20, 6, BG_PANEL);
            cb.popClip();
            cb.put(0, 0, 'Z', TEXT, BG_WINDOW);
            const bool cells = cb.at(3, 1).cp == 'Y' && cb.at(0, 0).cp == 'Z';
            const bool shapes = cb.shapes().size() == 1 && cb.shapes()[0].clip.x0 == 2 &&
                                cb.shapes()[0].clip.x1 == 7 && cb.shapes()[0].clip.y1 == 3;
            const bool surf = cb.surfaces().size() == 1 && cb.surfaces()[0].x == 2 &&
                              cb.surfaces()[0].w == 5 && cb.surfaces()[0].h == 2;
            check(cells && shapes && surf, "cells, shapes and surfaces all respect the clip");
        }

        // One frame of a scene, driven by a caller-built input.
        CellBuffer cb;
        cb.resize(40, 14, BG_WINDOW);
        auto run = [&](ui::Context& c, const ui::Input& in, auto&& body) {
            cb.clear(BG_WINDOW);
            c.begin(cb, in);
            body(c);
            c.end();
        };
        auto at = [](float x, float y) { ui::Input in; in.mouse_x = x; in.mouse_y = y; return in; };
        auto press = [&](float x, float y) {
            ui::Input in = at(x, y); in.mouse_down = true; in.mouse_pressed = true; return in; };
        auto hold = [&](float x, float y) { ui::Input in = at(x, y); in.mouse_down = true; return in; };
        auto release = [&](float x, float y) {
            ui::Input in = at(x, y); in.mouse_released = true; return in; };

        // A scroll region with 40 one-row buttons in a 10-row window.
        {
            ui::Context c;
            int fired = -1;
            auto list = [&](ui::Context& cc) {
                ui::BeginScroll(cc, "list", ui::Rect{ 0, 0, 20, 10 });
                for (int i = 0; i < 40; ++i) {
                    char label[8];
                    std::snprintf(label, sizeof label, "B%02d", i);
                    if (ui::Button(cc, label)) fired = i;
                    cc.newline();
                }
                ui::EndScroll(cc);
            };
            run(c, ui::Input{}, list);                        // measures the content
            ui::Input w = at(5, 5); w.wheel = -1.0f;
            run(c, w, list);
            const int afterOne = ui::ScrollOffset(c, "list");
            w.wheel = -100.0f;
            run(c, w, list);
            const int atEnd = ui::ScrollOffset(c, "list");
            check(afterOne == 3 && atEnd == 30,
                  "the wheel scrolls a region 3 rows a notch and stops at the end");

            // Row 0 of the screen now shows B30. A click there is B30's; B00
            // is scrolled away and must not be reachable through the clip.
            run(c, press(2.5f, 0.5f), list);
            run(c, release(2.5f, 0.5f), list);
            check(fired == 30, "a scrolled region clicks what is shown, not what is hidden");

            // Nothing drawn outside the region: row 10 onward stays blank.
            bool clean = true;
            for (int y = 10; y < cb.height(); ++y)
                for (int x = 0; x < cb.width(); ++x) clean = clean && cb.at(x, y).cp == ' ';
            check(clean, "scrolled content is clipped to its region");

            // Tab to the 15th button: the region follows the focus.
            ui::Context t;
            run(t, ui::Input{}, list);
            ui::Input tab; tab.key_tab = true;
            for (int i = 0; i < 15; ++i) run(t, tab, list);
            run(t, ui::Input{}, list);
            check(ui::ScrollOffset(t, "list") == 5, "Tab below the fold scrolls the focus into view");
        }

        // The scrollbar on its own: dragging the thumb, and paging.
        {
            ui::Context c;
            int off = 0;
            auto bar = [&](ui::Context& cc) { ui::ScrollbarV(cc, "sb", 0, 0, 10, 100, 10, off); };
            run(c, ui::Input{}, bar);
            run(c, press(0.5f, 0.8f), bar);
            run(c, hold(0.5f, 9.5f), bar);
            run(c, release(0.5f, 9.5f), bar);
            const int dragged = off;
            off = 0;
            ui::Context p;
            run(p, ui::Input{}, bar);
            run(p, press(0.5f, 6.0f), bar);
            run(p, release(0.5f, 6.0f), bar);
            check(dragged == 90 && off == 9,
                  "scrollbar: the thumb drags to the end, a click beside it pages");
        }

        // Toggles: click and Space both flip; a radio only ever sets.
        {
            ui::Context c;
            bool on = false;
            int sel = 0;
            auto body = [&](ui::Context& cc) {
                ui::Checkbox(cc, "Box", on);
                cc.newline();
                ui::Radio(cc, "A", sel, 0);
                cc.newline();
                ui::Radio(cc, "B", sel, 1);
            };
            run(c, press(1.5f, 0.5f), body);
            run(c, release(1.5f, 0.5f), body);
            const bool clicked = on;
            ui::Input tab; tab.key_tab = true;
            run(c, tab, body);                                // focus stays on Box? no: moves on
            ui::Input stab; stab.key_tab = true; stab.key_shift = true;
            run(c, stab, body);                               // back to Box
            ui::Input sp; sp.key_space = true;
            run(c, sp, body);
            const bool spaced = !on;
            run(c, press(1.5f, 2.5f), body);
            run(c, release(1.5f, 2.5f), body);
            const int picked = sel;
            run(c, press(1.5f, 2.5f), body);
            run(c, release(1.5f, 2.5f), body);
            check(clicked && spaced && picked == 1 && sel == 1,
                  "checkbox flips on click and Space; a radio sets and stays set");
        }

        // Sliders: the drag maps the pointer, keys step, discrete snaps,
        // a range never inverts.
        {
            ui::Context c;
            float v = 0.0f, s = 0.0f, a = 20.0f, b = 60.0f;
            auto body = [&](ui::Context& cc) {
                ui::Slider(cc, "v", 12, v);                    // row 0, cells 0..11
                cc.newline();
                ui::Slider(cc, "s", 12, s, 0.0f, 8.0f, 1.0f);  // row 1
                cc.newline();
                ui::SliderRange(cc, "r", 12, a, b, 0.0f, 100.0f, 1.0f);   // row 2
            };
            run(c, press(10.5f, 0.5f), body);
            const float atEnd = v;
            run(c, hold(6.0f, 0.5f), body);
            const float atHalf = v;
            run(c, release(6.0f, 0.5f), body);
            ui::Input right; right.key_right = true;          // v keeps the focus
            run(c, right, body);
            const bool stepped = std::fabs(v - 0.51f) < 1e-4f;

            run(c, press(5.3f, 1.5f), body);
            run(c, release(5.3f, 1.5f), body);
            const bool snapped = s == std::round(s) && s >= 0.0f && s <= 8.0f;

            // grab the left knob (a = 20 → cell 1 + 0.2 * 9 ≈ 3) and drag it
            // far past the right one
            run(c, press(3.5f, 2.5f), body);
            run(c, hold(11.0f, 2.5f), body);
            run(c, release(11.0f, 2.5f), body);
            check(atEnd == 1.0f && std::fabs(atHalf - 0.5f) < 1e-4f && stepped,
                  "slider: drag maps the pointer, → steps by 1 %");
            check(snapped && a <= b && a == 60.0f && b == 60.0f,
                  "slider: discrete snaps to steps, a range knob stops at the other");
        }

        // Slider ticks are not tied to cells: at any width they are evenly
        // spaced (to the pixel), and a knob on a step stands on its tick.
        {
            bool even = true, onTick = true;
            for (int w = 19; w <= 35; ++w) {        // ticks need 2 cells apart: 8 steps from 19
                ui::Context c;
                float v = 3.0f;
                run(c, ui::Input{}, [&](ui::Context& cc) { ui::Slider(cc, "s", w, v, 0.0f, 8.0f, 1.0f); });
                // Step ticks are the shapes shorter than the end ticks; the
                // knob is the square one.
                std::vector<float> ticks;
                float knob = -1.0f, endH = 0.0f;
                for (const Shape& s : cb.shapes()) endH = std::max(endH, s.h);
                for (const Shape& s : cb.shapes()) {
                    if (s.rfrac > 0.0f) knob = (s.x + s.w * 0.5f) * 9.0f;   // centre, px
                    else if (s.h > 0.1f && s.h < endH - 1e-4f) ticks.push_back((s.x + s.w * 0.5f) * 9.0f);
                }
                if (ticks.size() != 7) { even = false; continue; }
                std::sort(ticks.begin(), ticks.end());
                for (size_t i = 2; i < ticks.size(); ++i)
                    if (std::fabs((ticks[i] - ticks[i - 1]) - (ticks[1] - ticks[0])) > 1.01f) even = false;
                if (std::fabs(knob - ticks[2]) > 1.01f) onTick = false;   // value 3 = the third tick
            }
            check(even && onTick, "slider: ticks evenly spaced at any width, the knob stands on its tick");
        }

        // The split tree: drag, the minimum, double-click, persistence.
        {
            ui::Context c;
            ui::Layout lay;
            ui::Rect A, B;
            auto body = [&](ui::Context& cc) {
                ui::Split(cc, lay, "main", ui::Axis::Cols, ui::Rect{ 0, 0, 41, 10 },
                          0.5f, 5, 5, A, B);
            };
            run(c, ui::Input{}, body);
            const bool initial = A.w == 20 && B.x == 21 && B.w == 20;
            run(c, press(20.5f, 5.0f), body);
            run(c, hold(30.5f, 5.0f), body);
            const bool moved = A.w == 30 && B.x == 31;
            run(c, hold(40.0f, 5.0f), body);
            const bool stopped = A.w == 35 && B.w == 5;
            run(c, release(40.0f, 5.0f), body);
            // The pixel step: a quarter-cell move changes the fraction but
            // not the seam — the panels stay whole cells.
            const float before = lay.frac("main", 0.5f);
            run(c, press(35.5f, 5.0f), body);
            run(c, hold(35.25f, 5.0f), body);
            const bool subcell = lay.frac("main", 0.5f) != before && A.w == 35;
            run(c, release(35.25f, 5.0f), body);
            ui::Input dbl = press(35.5f, 5.0f); dbl.mouse_double = true;
            run(c, dbl, body);
            run(c, release(35.5f, 5.0f), body);
            const bool reset = A.w == 20;
            check(initial && moved && stopped && subcell && reset,
                  "splitter: drags by the pixel, lands on cells, stops at the minimum, "
                  "double-click resets");

            // Stacked: a one-row seam of empty window between the panels,
            // dragged like the column seam; no panel ground carried into it.
            ui::Rect T, U;
            auto rows = [&](ui::Context& cc) {
                ui::Split(cc, lay, "rows", ui::Axis::Rows, ui::Rect{ 0, 0, 20, 21 }, 0.5f, 3, 3, T, U);
            };
            ui::Context c2;
            run(c2, ui::Input{}, rows);
            const bool gapRow = T.h == 10 && U.y == 11 && U.h == 10;
            bool bare = true;
            for (const Shape& s : cb.shapes()) if (s.slot == BG_PANEL) bare = false;
            run(c2, press(5.0f, 10.5f), rows);
            run(c2, hold(5.0f, 13.5f), rows);
            run(c2, release(5.0f, 13.5f), rows);
            // The lower side's top frame row is marked to be drawn raised.
            bool raised = false;
            for (const CellBuffer::Raise& rz : cb.raises())
                if (rz.y == U.y && rz.x0 == U.x && rz.x1 == U.x + U.w) raised = true;
            check(gapRow && bare && T.h == 13 && U.y == 14 && U.h == 7 && raised,
                  "splitter: stacked panels keep an empty seam row, dragged like a column seam; "
                  "the lower top frame is raised");

            lay.frac("main", 0.5f) = 0.3125f;
            ui::Layout back;
            back.deserialize(lay.serialize() + ";junk=abc;bad=2;=0.5;ok=0.25");
            check(std::fabs(back.frac("main", 0.5f) - 0.3125f) < 1e-4f &&
                  std::fabs(back.frac("ok", 0.5f) - 0.25f) < 1e-4f &&
                  back.frac("junk", 0.7f) == 0.7f && back.frac("bad", 0.6f) == 0.6f,
                  "split fractions round-trip; corrupt entries fall back to defaults");
        }

        // ── text, table, tree (5.7–5.10) ────────────────────────────────────
        auto typed = [&](const char* s) {
            ui::Input in; in.text = Utf8Decode(s); return in; };
        auto key = [&](auto setter) { ui::Input in; setter(in); return in; };

        // The single-line field: typing, codepoints, selection, clipboard.
        {
            ui::Context c;
            std::string s;
            std::vector<std::string> hist;
            auto body = [&](ui::Context& cc) { ui::TextInput(cc, "f", 20, s, nullptr, 0, &hist); };
            run(c, press(3.0f, 0.5f), body);                   // click focuses it
            run(c, release(3.0f, 0.5f), body);
            run(c, typed("abc"), body);
            const bool typedOk = s == "abc" && c.textCaptured();
            s = "ёж";                                          // two codepoints, four bytes
            run(c, key([](ui::Input& i) { i.key_end = true; }), body);
            run(c, key([](ui::Input& i) { i.key_backspace = true; }), body);
            const bool codepoint = s == "ё";
            s = "hello";
            run(c, key([](ui::Input& i) { i.key_end = true; }), body);
            run(c, key([](ui::Input& i) { i.key_left = true; i.key_shift = true; }), body);
            run(c, typed("O"), body);
            const bool replaced = s == "hellO";
            run(c, key([](ui::Input& i) { i.key_select_all = true; i.key_copy = true; }), body);
            const bool copied = c.clipboardSet() && c.clipboardOut() == "hellO";
            run(c, key([](ui::Input& i) { i.key_select_all = true; i.key_cut = true; }), body);
            const bool cut = s.empty() && c.clipboardOut() == "hellO";
            run(c, key([](ui::Input& i) { i.key_paste = true; i.paste = "a\r\nb"; }), body);
            const bool pasted = s == "a b";                   // a line break joins a single line
            check(typedOk && codepoint && replaced && copied && cut && pasted,
                  "text field: types, deletes by codepoint, selects, copies, cuts, pastes");

            // History: Enter records, ↑ walks back, ↓ returns the draft.
            s = "one";
            run(c, key([](ui::Input& i) { i.key_enter = true; }), body);
            s = "two";
            run(c, key([](ui::Input& i) { i.key_enter = true; }), body);
            s = "dra";
            run(c, key([](ui::Input& i) { i.key_up = true; }), body);
            const bool back1 = s == "two";
            run(c, key([](ui::Input& i) { i.key_up = true; }), body);
            const bool back2 = s == "one";
            run(c, key([](ui::Input& i) { i.key_down = true; }), body);
            run(c, key([](ui::Input& i) { i.key_down = true; }), body);
            check(hist.size() == 2 && back1 && back2 && s == "dra",
                  "text field: Enter records history, ↑/↓ walk it and return the draft");

            // A field narrower than its text follows the cursor: the last
            // typed character is on screen, right before the cursor cell.
            ui::Context n;
            std::string longText;
            auto narrow = [&](ui::Context& cc) { ui::TextInput(cc, "n", 8, longText); };
            run(n, press(2.0f, 0.5f), narrow);
            run(n, release(2.0f, 0.5f), narrow);
            run(n, typed("abcdefghijklmnopqrstuvwxyZ"), narrow);
            // view is 6 cells (x 1..6); the cursor sits on the last one
            check(cb.at(5, 0).cp == 'Z', "text field: a long line scrolls to keep the cursor in view");
        }

        // The multi-line editor: Enter, the kept column, the status line.
        {
            ui::Context c;
            std::string s = "abcdef\nx\nabcdef";
            auto body = [&](ui::Context& cc) {
                ui::TextArea(cc, "ta", "T", ui::Rect{ 0, 0, 30, 9 }, s);
            };
            // Gutter: frame, 1 margin, 1 digit (3 lines), 2 blanks — text starts
            // at x = 5. Text starts right under the title, so line 0 is row 1.
            // Click line 0, col 5.
            run(c, press(10.0f, 1.5f), body);
            run(c, release(10.0f, 1.5f), body);
            run(c, key([](ui::Input& i) { i.key_down = true; }), body);
            run(c, key([](ui::Input& i) { i.key_down = true; }), body);
            run(c, typed("!"), body);
            const bool kept = s == "abcdef\nx\nabcde!f";
            run(c, key([](ui::Input& i) { i.key_enter = true; }), body);
            const bool split = s == "abcdef\nx\nabcde!\nf";
            // status row: y = h - 2 = 7
            std::string status;
            for (int x = 0; x < 30; ++x) Utf8Append(status, cb.at(x, 7).cp);
            check(kept && split && status.find("LN 4, COL 1") != std::string::npos,
                  "text area: ↑/↓ keep the column, Enter splits, the status line follows");
        }

        // The table: click selects, header sorts, keys move and scroll.
        {
            ui::Context c;
            int sel = -1, sortCol = -1;
            bool desc = false;
            int ev = 0;
            static const ui::TableColumn cols[] = { { "A", 4, true }, { "B", 8, false } };
            auto cellFn = [](void*, int row, int col) -> const char* {
                static char b[16];
                std::snprintf(b, sizeof b, col == 0 ? "%d" : "row%02d", row);
                return b;
            };
            auto body = [&](ui::Context& cc) {
                ev = ui::Table(cc, "t", "T", ui::Rect{ 0, 0, 24, 10 }, cols, 2, 20, cellFn,
                               nullptr, sel, sortCol, desc);
            };
            run(c, ui::Input{}, body);
            // The header is right under the title and the rows right
            // under the header: header at y = 1, rows from y = 2.
            run(c, press(5.0f, 4.5f), body);                   // row 2
            const bool picked = sel == 2 && (ev & ui::TBL_SELECT);
            run(c, release(5.0f, 4.5f), body);
            run(c, press(10.0f, 1.5f), body);                  // header of column B
            run(c, release(10.0f, 1.5f), body);
            const bool sorted = sortCol == 1 && !desc && (ev & ui::TBL_SORT);
            run(c, press(10.0f, 1.5f), body);
            run(c, release(10.0f, 1.5f), body);
            const bool flipped = desc;
            run(c, press(5.0f, 3.5f), body);                   // focus the rows again
            run(c, release(5.0f, 3.5f), body);
            run(c, key([](ui::Input& i) { i.key_end = true; }), body);
            run(c, ui::Input{}, body);
            std::string last;
            for (int x = 0; x < 24; ++x) Utf8Append(last, cb.at(x, 8).cp);   // last visible row
            check(picked && sorted && flipped, "table: a click selects, the header sorts and flips");
            check(sel == 19 && last.find("row19") != std::string::npos,
                  "table: End selects the last row and scrolls it into view");
        }

        // The bare table: header on the top row, rows from y = 2, no frame.
        {
            ui::Context c;
            int sel = -1, sortCol = -1;
            bool desc = false;
            static const ui::TableColumn cols[] = { { "A", 4, true }, { "B", 8, false } };
            auto cellFn = [](void*, int row, int col) -> const char* {
                static char b[16];
                std::snprintf(b, sizeof b, col == 0 ? "%d" : "row%02d", row);
                return b;
            };
            auto body = [&](ui::Context& cc) {
                ui::Table(cc, "t", "TITLE", ui::Rect{ 0, 0, 24, 8 }, cols, 2, 20, cellFn,
                          nullptr, sel, sortCol, desc, ui::TBL_BARE);
            };
            run(c, ui::Input{}, body);
            auto line = [&](int y) {
                std::string s;
                for (int x = 0; x < 24; ++x) Utf8Append(s, cb.at(x, y).cp);
                return s;
            };
            const bool noFrame = cb.at(0, 0).cp != 0x250C && line(0).find("TITLE") == std::string::npos &&
                                 line(0).find("rows") == std::string::npos;
            // No rule under the header either: the row right below it
            // is data, not ─.
            const bool header = line(0).find('A') != std::string::npos &&
                                line(0).find('B') != std::string::npos && cb.at(0, 1).cp != 0x2500;
            run(c, press(10.0f, 2.5f), body);                  // rows start at y = 1
            run(c, release(10.0f, 2.5f), body);
            check(noFrame && header && sel == 1 && line(1).find("row00") != std::string::npos,
                  "bare table: no frame, title or count; header on top, rows right under it");

            // Too narrow for its columns: the horizontal scrollbar goes into
            // the bottom margin row, centred in it, and costs no data row.
            ui::Context c2;
            auto narrow = [&](ui::Context& cc) {
                ui::Table(cc, "t", "TITLE", ui::Rect{ 0, 0, 12, 8 }, cols, 2, 20, cellFn,
                          nullptr, sel, sortCol, desc, ui::TBL_BARE);
            };
            run(c2, at(5.0f, 4.0f), narrow);              // the bar shows while hovered
            bool centred = false;
            for (const Shape& s : cb.shapes())
                if (s.w > s.h && std::fabs((s.y + s.h * 0.5f) - 7.5f) < 0.01f) centred = true;
            std::string row6;
            for (int x = 0; x < 12; ++x) Utf8Append(row6, cb.at(x, 6).cp);
            check(centred && row6.find('5') != std::string::npos,
                  "bare table: the horizontal scrollbar sits centred in the bottom margin row");

            // The bars belong to the region. Away from it, with the focus
            // elsewhere, neither is drawn; the rows are unchanged either way.
            run(c2, ui::Input{}, narrow);
            const size_t away = cb.shapes().size();
            std::string cold;
            for (int x = 0; x < 12; ++x) Utf8Append(cold, cb.at(x, 2).cp);
            run(c2, at(5.0f, 4.0f), narrow);
            const size_t over = cb.shapes().size();
            std::string warm;
            for (int x = 0; x < 12; ++x) Utf8Append(warm, cb.at(x, 2).cp);
            check(away == 0 && over == 2 && cold == warm,
                  "scrollbars: drawn only while the region is hovered or focused");
        }

        // The editable table: typing replaces, F2 edits in place, Shift+Enter
        // breaks the line and the row grows, Esc cancels, Tab moves on.
        {
            ui::Context c;
            std::vector<std::string> data = { "r0a", "r0b", "r1a", "r1b", "r2a", "r2b" };
            int selRow = 0, selCol = 0, sortCol = -1, evRow = -1, ev = 0;
            bool desc = false;
            const ui::EditColumn cols[] = { { "A", 6 }, { "B", 8 } };
            auto cellFn = [](void* u, int row, int col) -> std::string* {
                return &(*(std::vector<std::string>*)u)[(size_t)(row * 2 + col)];
            };
            auto body = [&](ui::Context& cc) {
                ev = ui::EditTable(cc, "et", "T", ui::Rect{ 0, 0, 30, 12 }, cols, 2, 3, cellFn,
                                   &data, selRow, selCol, sortCol, desc, evRow);
            };
            auto line = [&](int y) {
                std::string s;
                for (int x = 0; x < 30; ++x) Utf8Append(s, cb.at(x, y).cp);
                return s;
            };
            auto k = [&](auto setter) { run(c, key(setter), body); return ev; };
            run(c, ui::Input{}, body);
            // Rows start at y = 2; column A is x 1..8, B from x 10.
            run(c, press(12.0f, 3.5f), body);
            run(c, release(12.0f, 3.5f), body);
            const bool picked = selRow == 1 && selCol == 1;
            run(c, typed("x"), body);                            // typing replaces the cell
            const int evEnter = k([](ui::Input& i) { i.key_enter = true; });
            const bool replaced = data[3] == "x" && (evEnter & ui::ET_EDITED) && evRow == 1;
            k([](ui::Input& i) { i.key_f2 = true; });                   // F2 keeps the text
            run(c, typed("y"), body);
            k([](ui::Input& i) { i.key_enter = true; i.key_shift = true; });
            run(c, typed("z"), body);
            k([](ui::Input& i) { i.key_enter = true; });
            run(c, ui::Input{}, body);
            const bool multi = data[3] == "xy\nz";
            // Row 1 is now two lines tall: its second line at y = 4, row 2 at y = 5.
            const bool grown = line(4).find("z") != std::string::npos &&
                               line(5).find("r2a") != std::string::npos;
            k([](ui::Input& i) { i.key_enter = true; });
            run(c, typed("q"), body);
            k([](ui::Input& i) { i.key_escape = true; });
            const bool cancelled = data[3] == "xy\nz";
            selRow = 0; selCol = 0;
            k([](ui::Input& i) { i.key_f2 = true; });
            k([](ui::Input& i) { i.key_tab = true; });                  // commit, next cell
            run(c, typed("!"), body);
            k([](ui::Input& i) { i.key_enter = true; });
            const bool tabbed = selRow == 0 && selCol == 1 && data[1] == "r0b!";
            check(picked && replaced, "edit table: a click picks a cell, typing replaces it");
            check(multi && grown, "edit table: Shift+Enter breaks the line and the row grows");
            check(cancelled && tabbed, "edit table: Esc cancels, Tab commits and moves on");

            const int ins = k([](ui::Input& i) { i.key_ctrl = true; i.key_insert = true; });
            const int insRow = evRow;
            const int del = k([](ui::Input& i) { i.key_ctrl = true; i.key_delete = true; });
            const int delRow = evRow;
            run(c, press(3.0f, 1.5f), body);                     // header of column A
            run(c, release(3.0f, 1.5f), body);
            check((ins & ui::ET_INSERT) && insRow == 1 && (del & ui::ET_DELETE) && delRow == 0 &&
                  (ev & ui::ET_SORT) && sortCol == 0,
                  "edit table: Ctrl+Ins / Ctrl+Del report rows, the header sorts");
        }

        // Cell fractions: the progress fill is a shape, so it
        // grows by fractions of a pixel. Never backwards, never a jump — that
        // is what rounding to eighths of a cell would look like.
        {
            CellBuffer pb;
            pb.resize(24, 1, BG_WINDOW);
            float last = -1.0f, worstJump = 0.0f;
            bool monotone = true;
            for (int i = 0; i <= 1000; ++i) {
                pb.clear(BG_WINDOW);
                ui::Context c;
                c.begin(pb, ui::Input{});
                ui::ProgressBar(c, 20, (float)i / 1000.0f);
                c.end();
                const float w = pb.shapes().size() > 1 ? pb.shapes()[1].w : 0.0f;  // track, then fill
                if (w + 1e-4f < last) monotone = false;
                if (last >= 0.0f) worstJump = std::max(worstJump, (w - last) * 9.0f);   // px
                last = w;
            }
            check(monotone && worstJump <= 1.0f && last > 19.0f,
                  "cell fractions: the progress fill grows smoothly to the full bar");
            if (!monotone || worstJump > 1.0f)
                std::printf("      monotone=%d, worst jump %.2f px\n", (int)monotone, worstJump);
        }

        // Drawing on demand (§9): a frame asks for the next one only when
        // something on it moves by itself. Miss that and the loop sleeps.
        {
            CellBuffer ab;
            ab.resize(20, 3, BG_WINDOW);
            ui::Context c;
            auto frame = [&](auto&& body) {
                ab.clear(BG_WINDOW);
                ui::Input in;
                in.time = 1.0;
                c.begin(ab, in);
                body(c);
                c.end();
            };
            frame([](ui::Context& cc) { ui::Label(cc, "still"); });
            const bool quiet = c.nextFrameIn() < 0.0;
            frame([](ui::Context& cc) { ui::ProgressBusy(cc, 10); });
            const double busyIn = c.nextFrameIn();                // 30 Hz while it travels
            const bool busy = busyIn >= 0.0 && busyIn <= 1.0 / 30.0;
            frame([](ui::Context& cc) { (void)cc.blinkOn(); });
            const double blinkIn = c.nextFrameIn();
            check(quiet && busy && blinkIn > 0.0 && blinkIn <= 0.53,
                  "a frame asks for the next one only when something moves");
        }

        // The rule over the status bar is drawn half a row up; the one under
        // the menu stays where its row is.
        {
            CellBuffer rb;
            rb.resize(20, 6, BG_WINDOW);
            chrome::DrawRule(rb, 1);
            chrome::DrawRule(rb, 4, /*raised=*/true);
            const bool one = rb.raises().size() == 1 && rb.raises()[0].y == 4 &&
                             rb.raises()[0].by == CellBuffer::RAISE_HALF && rb.raises()[0].x1 == 20;
            check(one && rb.at(0, 4).cp == Gly::H, "the rule over the status bar is raised half a row");
        }

        // A panel's ground stops at its frame line: DrawFrame marks all four
        // edges framed; an edge a clip has cut keeps no frame to stop at.
        {
            CellBuffer fb;
            fb.resize(20, 10, BG_WINDOW);
            ui::DrawFrame(fb, ui::Rect{ 2, 2, 10, 5 }, BORDER, BG_PANEL);
            const bool whole = !fb.surfaces().empty() && fb.surfaces().back().framed == EDGE_ALL;
            fb.pushClip(0, 0, 20, 4);                       // cuts the bottom edge off
            ui::DrawFrame(fb, ui::Rect{ 2, 2, 10, 5 }, BORDER, BG_PANEL);
            fb.popClip();
            const bool cut = fb.surfaces().back().framed == (EDGE_L | EDGE_R | EDGE_T);
            // "[ ABC ]" gets a cap of ground above the line: 7 cells, one row.
            ui::DrawFrameTitle(fb, 2, 2, "ABC", BORDER, BG_PANEL, ACCENT);
            const Surface& cap = fb.surfaces().back();
            const bool capped = cap.framed == (EDGE_CAP | EDGE_INK_L | EDGE_INK_R) && cap.x == 3 && cap.y == 2 && cap.w == 7 && cap.h == 1;
            check(whole && cut && capped,
                  "panel ground is framed on every edge but one a clip has cut; a title gets a cap");
            // A popup (menu, list, tooltip) is the same: its own framed ground
            // in the overlay band, so its raised background stops at its frame.
            fb.setBand(Z_OVERLAY);
            ui::DrawFrame(fb, ui::Rect{ 4, 3, 8, 4 }, BORDER_FOCUS, BG_RAISED);
            fb.setBand(Z_CONTENT);
            const Surface& pop = fb.surfaces().back();
            check(pop.z == Z_OVERLAY && pop.slot == BG_RAISED && pop.framed == EDGE_ALL,
                  "a popup's raised ground is framed like a panel's");
        }

        // Charts (§8.20): lines are segments, the smooth one never overshoots,
        // bars are rectangles, the axes and the legend are glyphs.
        {
            ui::Context c;
            CellBuffer gb;
            gb.resize(50, 14, BG_WINDOW);
            auto frame = [&](const ui::Input& in, auto&& body) {
                gb.clear(BG_WINDOW);
                c.begin(gb, in);
                body(c);
                c.end();
            };
            auto segments = [&](float& yMin) {
                int n = 0;
                yMin = 1e9f;
                for (const Shape& s : gb.shapes())
                    if (s.kind == SHAPE_SEGMENT) { ++n; yMin = std::min({ yMin, s.y, s.y + s.h }); }
                return n;
            };
            auto row = [&](int y) {
                std::string s;
                for (int x = 0; x < 50; ++x) Utf8Append(s, gb.at(x, y).cp);
                return s;
            };
            static const float kV[] = { 0.0f, 100.0f, 60.0f, 0.0f };   // a lopsided peak
            static const char* kL[] = { "A", "B", "C", "D" };
            const ui::ChartSeries one[1] = { { "load", kV, CHART_1 } };
            const ui::Rect r{ 0, 0, 50, 14 };

            float peakStraight = 0.0f, peakSmooth = 0.0f;
            frame(ui::Input{}, [&](ui::Context& cc) { ui::LineChart(cc, "l", "T", r, one, 1, 4, kL, 0.0f, 100.0f); });
            const int straight = segments(peakStraight);
            const bool axes = row(0).find("■ load") != std::string::npos &&
                              row(11).find("0 └") != std::string::npos && row(12).find('B') != std::string::npos;
            frame(ui::Input{}, [&](ui::Context& cc) {
                ui::LineChart(cc, "l", "T", r, one, 1, 4, kL, 0.0f, 100.0f, ui::CHART_SMOOTH);
            });
            const int smooth = segments(peakSmooth);
            check(straight == 3 && axes, "line chart: a segment per run, axes and legend as glyphs");
            check(smooth > 3 * 2 && peakSmooth >= peakStraight - 1e-3f,
                  "line chart: the smooth curve is many segments and never overshoots the data");

            // Hovering the plot puts a dot on the series.
            ui::Input hover; hover.mouse_x = 30.5f; hover.mouse_y = 6.5f;
            auto dots = [&]() {
                int n = 0;
                for (const Shape& s : gb.shapes()) if (s.kind == SHAPE_RECT && s.rfrac == 0.5f && s.slot == CHART_1) ++n;
                return n;
            };
            frame(hover, [&](ui::Context& cc) { ui::LineChart(cc, "l", "T", r, one, 1, 4, kL, 0.0f, 100.0f); });
            frame(hover, [&](ui::Context& cc) { ui::LineChart(cc, "l", "T", r, one, 1, 4, kL, 0.0f, 100.0f); });
            check(dots() == 1, "line chart: hovering marks the nearest point");

            // Bars: one rectangle per value, taller (wider) for a larger value.
            static const float kB[] = { 10.0f, 40.0f };
            const ui::ChartSeries bars[1] = { { "n", kB, CHART_2 } };
            auto barShapes = [&]() {
                std::vector<Shape> v;
                for (const Shape& s : gb.shapes()) if (s.slot == CHART_2 && s.kind == SHAPE_RECT) v.push_back(s);
                return v;
            };
            frame(ui::Input{}, [&](ui::Context& cc) { ui::BarChart(cc, "b", "T", r, bars, 1, 2, kL); });
            const std::vector<Shape> vb = barShapes();
            frame(ui::Input{}, [&](ui::Context& cc) {
                ui::BarChart(cc, "b", "T", r, bars, 1, 2, kL, 0.0f, ui::CHART_HORIZONTAL);
            });
            const std::vector<Shape> hb = barShapes();
            check(vb.size() == 2 && vb[1].h > vb[0].h * 3.5f && hb.size() == 2 && hb[1].w > hb[0].w * 3.5f,
                  "bar chart: a rectangle per value, vertical and horizontal, to scale");
        }

        // Diagrams (§8.21): a box per node and a line per edge, hidden nodes
        // keep their place, the view zooms about the cursor, drags, resets.
        {
            ui::Context c;
            CellBuffer gb;
            gb.resize(80, 14, BG_WINDOW);
            auto frame = [&](const ui::Input& in, auto&& body) {
                gb.clear(BG_WINDOW);
                c.begin(gb, in);
                body(c);
                c.end();
            };
            auto row = [&](int y) {
                std::string s;
                for (int x = 0; x < gb.width(); ++x) Utf8Append(s, gb.at(x, y).cp);
                return s;
            };
            auto count = [&](auto&& pred) {
                int n = 0;
                for (const Shape& s : gb.shapes()) if (pred(s)) ++n;
                return n;
            };
            auto isSeg = [](const Shape& s) { return s.kind == SHAPE_SEGMENT; };
            // The boxes' grounds, in node order (hidden ones skipped).
            auto boxes = [&]() {
                std::vector<Shape> v;
                for (const Shape& s : gb.shapes())
                    if (s.kind == SHAPE_RECT && s.slot == BG_RAISED && s.rfrac == 0.35f) v.push_back(s);
                return v;
            };
            auto at = [](float x, float y) { ui::Input i; i.mouse_x = x; i.mouse_y = y; return i; };

            // (a) + (b × c): three leaves, two levels below the root.
            std::vector<ui::DiagramNode> t(5);
            const char* lab[5] = { "+", "a", "×", "b", "c" };
            const int par[5] = { -1, 0, 0, 2, 2 };
            for (int i = 0; i < 5; ++i) { t[(size_t)i].label = lab[i]; t[(size_t)i].parent = par[i]; }
            const ui::Rect r{ 0, 0, 80, 14 };
            int hov = -2;
            auto tree = [&](const ui::Input& in) {
                frame(in, [&](ui::Context& cc) { hov = ui::TreeDiagram(cc, "t", r, t.data(), 5); });
            };
            tree(ui::Input{});
            const std::vector<Shape> b0 = boxes();
            const bool laidOut = b0.size() == 5 && count(isSeg) == 4 &&
                                 b0[0].y < b0[1].y && b0[1].y == b0[2].y && b0[2].y < b0[3].y &&
                                 b0[1].x < b0[3].x && b0[3].x < b0[4].x &&
                                 std::fabs((b0[2].x + b0[2].w * 0.5f) - (b0[3].x + b0[4].x + b0[4].w) * 0.5f) < 1.0f &&
                                 row(13).find(str::T(str::ID_DIAG_HINT)) != std::string::npos;
            check(laidOut, "tree diagram: a box per node, a line per edge, a parent over its children, the hint");
            t[2].hidden = true;
            tree(ui::Input{});
            const std::vector<Shape> bh = boxes();
            check(bh.size() == 4 && count(isSeg) == 1 && bh[2].x == b0[3].x,
                  "tree diagram: a hidden node loses its box and its lines, the rest keep their places");
            t[2].hidden = false;

            // Hover: the box under the pointer is reported.
            const float ax = b0[1].x + b0[1].w * 0.5f, ay = b0[1].y + 0.4f;
            tree(at(ax, ay));
            tree(at(ax, ay));
            check(hov == 1, "tree diagram: the node under the pointer is returned");

            // The wheel, one notch on the root: the leaves spread apart, the root
            // stays under the cursor (what leaves the area is culled, so the
            // zoom is kept small enough for every box to stay in it).
            const float rx = b0[0].x + b0[0].w * 0.5f, ry = b0[0].y + 0.4f;
            ui::Input wh = at(rx, ry); wh.wheel = 1.0f;
            tree(wh);
            const std::vector<Shape> bz = boxes();
            const float spread0 = b0[4].x - b0[1].x, spread1 = bz.size() == 5 ? bz[4].x - bz[1].x : 0.0f;
            check(bz.size() == 5 && std::fabs(spread1 / spread0 - 1.25f) < 0.05f &&
                  std::fabs(bz[0].x + bz[0].w * 0.5f - rx) < 1.0f && bz[0].y == b0[0].y && bz[1].w == b0[1].w,
                  "tree diagram: the wheel zooms about the cursor, the boxes keep their size");

            // A double click shows it whole again; a drag moves it by as much as the pointer.
            ui::Input dbl = at(70.5f, 12.5f); dbl.mouse_pressed = dbl.mouse_down = dbl.mouse_double = true;
            tree(dbl);
            ui::Input up = at(70.5f, 12.5f); up.mouse_released = true;
            tree(up);
            const bool whole = boxes().size() == 5 && boxes()[1].x == b0[1].x && boxes()[4].x == b0[4].x;
            ui::Input pr = at(60.5f, 11.5f); pr.mouse_pressed = pr.mouse_down = true;
            tree(pr);
            ui::Input mv = at(65.5f, 12.5f); mv.mouse_down = true;
            tree(mv);
            const bool moved = boxes().size() == 5 && boxes()[1].x == b0[1].x + 5.0f && boxes()[1].y == b0[1].y + 1.0f;
            ui::Input rl = at(65.5f, 12.5f); rl.mouse_released = true;
            tree(rl);
            frame(ui::Input{}, [&](ui::Context& cc) {
                ui::ResetDiagram(cc, "t");
                ui::TreeDiagram(cc, "t", r, t.data(), 5);
            });
            const bool reset = boxes()[1].x == b0[1].x && boxes()[1].y == b0[1].y;
            check(whole && moved && reset,
                  "tree diagram: a double click shows it whole, a drag moves it, ResetDiagram too");

            // The network: a line per weight above 8 % of the largest, its sign
            // in its colour; a node per neuron; the outer layers' names.
            static const float kW0[] = { 1.0f, -0.5f, 0.05f, 0.9f, -1.0f, 0.3f };   // 3 × 2, one too faint
            static const float kW1[] = { 0.5f, -0.25f, 0.0f };                    // 1 × 3, one zero
            static const char* kIn[] = { "x", "y" };
            static const char* kOut[] = { "P" };
            const ui::NetLayer layers[3] = { { 2, ACCENT, kIn }, { 3, TEXT2 }, { 1, SUCCESS, kOut } };
            const float* w[2] = { kW0, kW1 };
            auto nodes = [&]() {
                std::vector<Shape> v;
                for (const Shape& s : gb.shapes()) if (s.kind == SHAPE_RECT && s.rfrac == 0.5f) v.push_back(s);
                return v;
            };
            frame(ui::Input{}, [&](ui::Context& cc) { ui::NetDiagram(cc, "n", r, layers, 3, w); });
            const std::vector<Shape> n0 = nodes();
            const int pos = count([](const Shape& s) { return s.kind == SHAPE_SEGMENT && s.slot == CHART_1; });
            const int neg = count([](const Shape& s) { return s.kind == SHAPE_SEGMENT && s.slot == CHART_2; });
            bool inName = false, outName = false;      // "x" left of the first node, "P" right of the last
            for (int y = 0; y < 13; ++y) {
                const std::string s = row(y);
                inName = inName || (s.find('x') != std::string::npos && (int)s.find('x') < (int)n0[0].x);
                outName = outName || (s.find('P') != std::string::npos && (int)s.find('P') > (int)(n0[5].x + n0[5].w));
            }
            check(pos == 4 && neg == 3 && n0.size() == 6 && inName && outName,
                  "net diagram: a line per weight in the colour of its sign, the faint ones dropped, a node per neuron");
            frame(ui::Input{}, [&](ui::Context& cc) { ui::NetDiagram(cc, "u", r, layers, 3, nullptr); });
            check(count([](const Shape& s) { return s.kind == SHAPE_SEGMENT && s.slot == BORDER_DIM; }) == 9,
                  "net diagram: without weights, every connection alike");
            // Zoomed about the middle: the hidden layer's column spreads, its
            // nodes grow as the zoom's square root (the outer layers, near
            // the edges, leave the area).
            auto hidden = [&]() {
                std::vector<Shape> v;
                for (const Shape& s : nodes()) if (s.slot == TEXT2) v.push_back(s);
                return v;
            };
            frame(ui::Input{}, [&](ui::Context& cc) { ui::NetDiagram(cc, "n", r, layers, 3, w); });
            const std::vector<Shape> h0 = hidden();
            ui::Input nz = at(40.0f, 6.5f); nz.wheel = 1.0f;
            frame(nz, [&](ui::Context& cc) { ui::NetDiagram(cc, "n", r, layers, 3, w); });
            const std::vector<Shape> h1 = hidden();
            check(h0.size() == 3 && h1.size() == 3 &&
                  std::fabs((h1[2].y - h1[0].y) / (h0[2].y - h0[0].y) - 1.25f) < 0.01f &&
                  std::fabs(h1[0].w / h0[0].w - std::sqrt(1.25f)) < 0.01f,
                  "net diagram: the wheel spreads the layers, the nodes grow as its square root");
        }

        // The tree: connectors as text, and the keys.
        {
            std::vector<ui::TreeNode> roots(1);
            ui::TreeNode& a = roots[0];
            a.label = "a"; a.open = true;
            a.children.resize(2);
            ui::TreeNode& b = a.children[0];
            b.label = "b"; b.open = true;
            b.children.resize(2);
            b.children[0].label = "c";
            b.children[1].label = "d";
            a.children[1].label = "e";
            const ui::TreeNode* sel = nullptr;

            ui::Context c;
            CellBuffer tb;
            tb.resize(24, 6, BG_WINDOW);
            auto frame = [&](const ui::Input& in) {
                tb.clear(BG_WINDOW);
                c.begin(tb, in);
                ui::Tree(c, "tree", ui::Rect{ 0, 0, 24, 6 }, roots, sel);
                c.end();
            };
            frame(ui::Input{});
            std::string got;
            for (int y = 0; y < 5; ++y) {
                std::string line;
                for (int x = 0; x < 24; ++x) Utf8Append(line, tb.at(x, y).cp);
                while (!line.empty() && line.back() == ' ') line.pop_back();
                got += line + "\n";
            }
            const std::string want =
                " ▼ a\n"
                "   ├─▼ b\n"
                "   │   ├── c\n"
                "   │   └── d\n"
                "   └── e\n";
            check(got == want, "tree: ├── └── │ connectors and ▼ folders, exactly");
            if (got != want) std::printf("      got:\n%s", got.c_str());

            // Click "b", ← closes it, ← again steps out to "a", → steps back in.
            ui::Input p = press(8.0f, 1.5f);
            frame(p);
            ui::Input rl = release(8.0f, 1.5f);
            frame(rl);
            const bool clicked = sel == &b;
            frame(key([](ui::Input& i) { i.key_left = true; }));
            const bool closed = !b.open;
            frame(key([](ui::Input& i) { i.key_left = true; }));
            const bool up = sel == &a;
            frame(key([](ui::Input& i) { i.key_right = true; }));
            const bool in = sel == &a.children[0];
            // Folding a folder that hides the selection moves it to the folder.
            b.open = true;
            sel = &b.children[1];
            frame(ui::Input{});
            frame(press(1.5f, 0.5f));                          // the ▼ of "a"
            frame(release(1.5f, 0.5f));
            check(clicked && closed && up && in && !a.open && sel == &a,
                  "tree: click, ←/→ fold and walk; folding the selection away selects the folder");
        }

        // ── overlays and tabs (5.11–5.14) ───────────────────────────────────
        auto topRow = [](const CellBuffer& b, int y) {
            std::string s;
            for (int x = 0; x < b.width(); ++x) Utf8Append(s, b.top(x, y).cp);
            return s;
        };

        // Dropdown: the list stays on top of a panel drawn AFTER it, and it
        // blocks the button under it.
        {
            ui::Context c;
            int sel = 0, clicks = 0;
            static const char* items[] = { "Alpha", "Beta", "Gamma" };
            auto body = [&](ui::Context& cc) {
                // A button drawn BEFORE the dropdown, right where its list
                // will open: in draw order it gets the press first, so only
                // the overlay's claim on the area can keep it from firing.
                cc.pushRegion(ui::Rect{ 2, 4, 20, 1 });
                if (ui::Button(cc, "UNDER")) ++clicks;                // row 4, x 2..10
                cc.popRegion();
                cc.pushRegion(ui::Rect{ 0, 0, 40, 14 });
                ui::Dropdown(cc, "dd", 14, items, 3, sel);            // row 0
                cc.popRegion();
                // A panel drawn AFTER it, over the rows the list opens on.
                ui::BeginPanel(cc, "p", "PANEL", ui::Rect{ 8, 1, 20, 6 });  // covers x 8..27
                ui::EndPanel(cc);
            };
            run(c, press(3.0f, 0.5f), body);
            run(c, release(3.0f, 0.5f), body);                         // opens
            run(c, ui::Input{}, body);
            const std::string r2 = topRow(cb, 2), r3 = topRow(cb, 3);
            // x = 11 is where the ✓ of the current item sits — inside the panel
            // drawn after the list. The overlay grid keeps it.
            const bool onTop = r2.find("Alpha") != std::string::npos &&
                               cb.top(11, 2).cp == Gly::CHECK &&
                               r3.find("Beta") != std::string::npos;
            run(c, press(4.0f, 4.5f), body);                           // Gamma's row, over UNDER
            run(c, release(4.0f, 4.5f), body);
            check(onTop && sel == 2 && clicks == 0,
                  "dropdown: the list stays on top of later panels and blocks what is under it");

            // Keyboard: the field has the focus; Enter opens, ↓ moves, Enter picks.
            run(c, key([](ui::Input& i) { i.key_enter = true; }), body);
            run(c, key([](ui::Input& i) { i.key_down = true; }), body);  // wraps? no: clamps at 2
            run(c, key([](ui::Input& i) { i.key_up = true; }), body);
            run(c, key([](ui::Input& i) { i.key_enter = true; }), body);
            const bool keyed = sel == 1;
            run(c, key([](ui::Input& i) { i.key_enter = true; }), body);
            run(c, key([](ui::Input& i) { i.key_escape = true; }), body);
            const bool escKept = c.escapeClaimed();
            run(c, ui::Input{}, body);
            check(keyed && escKept && topRow(cb, 2).find("Alpha") == std::string::npos,
                  "dropdown: Enter/↑/↓ pick, Esc closes it and does not reach the app");
        }

        // Context menu: measures, opens, highlights, chooses; submenu and Esc.
        {
            ui::Context c;
            int chosen = -1;
            bool hidden = false;
            int subPicked = -1;
            auto body = [&](ui::Context& cc) {
                if (ui::BeginContextMenu(cc, "m", ui::Rect{ 0, 0, 40, 14 })) {
                    if (ui::MenuItem(cc, "Open", "Enter")) chosen = 0;
                    if (ui::MenuItem(cc, "Rename", "F2")) chosen = 1;
                    ui::MenuSeparator(cc);
                    ui::MenuCheck(cc, "Hidden", hidden);
                    if (ui::BeginSubmenu(cc, "Sort")) {
                        if (ui::MenuItem(cc, "Name")) subPicked = 0;
                        ui::EndSubmenu(cc);
                    }
                    ui::EndContextMenu(cc);
                }
            };
            ui::Input rp = at(2.5f, 1.5f); rp.mouse_right_pressed = true;
            run(c, rp, body);                                          // opens, measures
            run(c, ui::Input{}, body);                                 // draws
            const bool drawn = topRow(cb, 2).find("Open") != std::string::npos &&
                               topRow(cb, 2).find("Enter") != std::string::npos &&
                               topRow(cb, 4).find("├") != std::string::npos;
            // keyboard: ↓ ↓ Enter = "Rename"
            run(c, key([](ui::Input& i) { i.key_down = true; }), body);
            run(c, key([](ui::Input& i) { i.key_down = true; }), body);
            run(c, key([](ui::Input& i) { i.key_enter = true; }), body);
            const bool byKeys = chosen == 1;
            run(c, ui::Input{}, body);
            const bool closed = topRow(cb, 2).find("Open") == std::string::npos;

            // reopen, walk to "Sort", → opens it, Esc closes ONLY the submenu
            run(c, rp, body);
            run(c, ui::Input{}, body);
            for (int k = 0; k < 4; ++k) run(c, key([](ui::Input& i) { i.key_down = true; }), body);
            run(c, key([](ui::Input& i) { i.key_right = true; }), body);
            run(c, ui::Input{}, body);                                 // submenu measures
            run(c, ui::Input{}, body);                                 // and draws
            bool subShown = false;
            for (int y = 0; y < cb.height(); ++y)
                subShown = subShown || topRow(cb, y).find("Name") != std::string::npos;
            run(c, key([](ui::Input& i) { i.key_escape = true; }), body);
            run(c, ui::Input{}, body);
            bool subGone = true, rootStill = false;
            for (int y = 0; y < cb.height(); ++y) {
                subGone = subGone && topRow(cb, y).find("Name") == std::string::npos;
                rootStill = rootStill || topRow(cb, y).find("Open") != std::string::npos;
            }
            // a click outside closes the rest
            run(c, press(38.0f, 13.0f), body);
            run(c, release(38.0f, 13.0f), body);
            bool allGone = true;
            for (int y = 0; y < cb.height(); ++y)
                allGone = allGone && topRow(cb, y).find("Open") == std::string::npos;
            check(drawn && byKeys && closed, "context menu: opens at the pointer, ↓/Enter choose, it closes");
            check(subShown && subGone && rootStill && allGone && subPicked < 0,
                  "context menu: → opens a submenu, Esc closes only it, a click outside closes all");
        }

        // Modal: Tab is trapped inside, the content is inert and dimmed.
        {
            ui::Context c;
            bool open = false;
            int under = 0, ok = 0, no = 0;
            auto body = [&](ui::Context& cc) {
                cc.pushRegion(ui::Rect{ 0, 0, 40, 14 });
                if (ui::Button(cc, "BEHIND")) { ++under; open = true; }
                cc.popRegion();
                if (ui::BeginModal(cc, "mdl", "ASK", 24, 7, open)) {
                    if (ui::Button(cc, "OK")) ++ok;
                    cc.spacer(1);
                    if (ui::Button(cc, "NO", ui::BTN_DEFAULT)) ++no;
                    ui::EndModal(cc);
                }
            };
            run(c, press(2.0f, 0.5f), body);
            run(c, release(2.0f, 0.5f), body);                         // opens it
            run(c, ui::Input{}, body);
            // Enter straight after opening presses the DEFAULT button, not the
            // first one: in a destructive dialog that is the safe answer.
            run(c, key([](ui::Input& i) { i.key_enter = true; }), body);
            const bool safeDefault = no == 1 && ok == 0;
            // the content button can no longer be pressed
            run(c, press(2.0f, 0.5f), body);
            run(c, release(2.0f, 0.5f), body);
            const bool inert = under == 1;
            // Tab three times from wherever: focus must never leave the modal
            bool trapped = true;
            for (int k = 0; k < 3; ++k) {
                run(c, key([](ui::Input& i) { i.key_tab = true; }), body);
                trapped = trapped && c.focusedId() != ui::kNoId;
            }
            // the focus is on a modal button: Enter on "OK" or "NO"
            run(c, key([](ui::Input& i) { i.key_tab = true; }), body);   // lands on NO or OK
            const bool dimmed = (cb.top(0, 13).attr & ATTR_DIM) != 0;     // a blank corner, dimmed
            run(c, key([](ui::Input& i) { i.key_escape = true; }), body);
            run(c, ui::Input{}, body);
            check(safeDefault && inert && trapped && dimmed && !open && !cb.dimBase(),
                  "modal: focus starts on the default, content inert and dimmed, Tab stays in, Esc closes");
        }

        // Tooltip: only after the delay, and it does not steal the hover.
        {
            ui::Context c;
            auto body = [&](ui::Context& cc) {
                ui::Button(cc, "HOVER");
                ui::Tooltip(cc, "tip text");
            };
            ui::Input h = at(2.0f, 0.5f);
            h.time = 10.0; run(c, h, body);
            h.time = 10.1; run(c, h, body);
            bool early = false;
            for (int y = 0; y < cb.height(); ++y)
                early = early || topRow(cb, y).find("tip text") != std::string::npos;
            h.time = 10.7; run(c, h, body);
            h.time = 10.8; run(c, h, body);
            bool late = false;
            for (int y = 0; y < cb.height(); ++y)
                late = late || topRow(cb, y).find("tip text") != std::string::npos;
            check(!early && late, "tooltip: appears after ~450 ms of hover, not before, and stays");
        }

        // Glitch: a field going into error jolts, settles within ~0.2 s, and
        // staying in error does not jolt again.
        {
            ui::Context c;
            std::string v = "80800";
            bool err = false;
            auto body = [&](ui::Context& cc) {
                ui::TextInput(cc, "g", 12, v, nullptr, err ? ui::TXT_ERROR : ui::TXT_NONE);
            };
            auto frameAt = [&](double t) { ui::Input i; i.time = t; run(c, i, body); return topRow(cb, 0); };
            frameAt(10.00);
            err = true;
            const std::string jolt = frameAt(10.05);       // just went into error
            const std::string settled = frameAt(10.40);    // past the glitch
            const std::string still = frameAt(11.00);      // still in error: no replay
            check(jolt != settled && settled == still && settled.find("80800") != std::string::npos,
                  "glitch: the field jolts as it goes into error, settles, and does not replay");

            // In a showcase (forced state) it loops: glitch 0.21 s, pause 0.2 s.
            ui::Context f;
            auto forcedAt = [&](double t) {
                ui::Input i; i.time = t;
                cb.clear(BG_WINDOW);
                f.begin(cb, i);
                f.beginForced(ui::FORCE_NONE);
                ui::TextInput(f, "g", 12, v, nullptr, ui::TXT_ERROR);
                f.endForced();
                f.end();
                return topRow(cb, 0);
            };
            // Period 0.41 s: glitching for t in [0, 0.21), pause in [0.21, 0.41).
            const std::string first = forcedAt(20.00);     // shown in error: plays
            const std::string pause = forcedAt(20.25);     // the pause
            const std::string again = forcedAt(20.45);     // second cycle: plays again
            const std::string pause2 = forcedAt(20.70);    // second pause
            check(first != settled && pause == settled && again != settled && pause2 == settled,
                  "glitch: in a showcase the error glitch loops with a pause");

            // With glitches off nothing plays, not even on the event.
            ui::Context off;
            bool e2 = false;
            auto offAt = [&](double t) {
                ui::Input i; i.time = t;
                cb.clear(BG_WINDOW);
                off.begin(cb, i);
                off.setGlitches(false);
                ui::TextInput(off, "g", 12, v, nullptr, e2 ? ui::TXT_ERROR : ui::TXT_NONE);
                off.end();
                return topRow(cb, 0);
            };
            offAt(30.00);
            e2 = true;
            check(offAt(30.05) == settled, "glitch: switched off, the field does not jolt");
        }

        // Whole-window glitches: a tear shifts rows of everything, overlays
        // too; the danger-modal jolt shifts only what lies under the modal;
        // one at a time, the next free as soon as it ends; none with glitches off.
        {
            auto screenAt = [&](ui::Context& c, double t, bool fault, bool modal, bool on) {
                ui::Input i; i.time = t;
                cb.clear(BG_WINDOW);
                c.begin(cb, i);
                c.setGlitches(on);
                for (int y = 0; y < cb.height(); ++y)                  // a full, striped screen
                    for (int x = 0; x < cb.width(); ++x)
                        cb.put(x, y, (uint32_t)('A' + (x + y) % 26), TEXT, BG_WINDOW);
                if (fault) c.glitchScreen(ui::Context::ScreenGlitch::Tear);
                bool open = modal;
                if (ui::BeginModal(c, "m", "M", 20, 6, open, ui::MODAL_DANGER)) ui::EndModal(c);
                c.end();
                std::string all;
                for (int y = 0; y < cb.height(); ++y) all += topRow(cb, y) + "\n";
                return all;
            };
            ui::Context c;
            const std::string plain = screenAt(c, 40.00, false, false, true);
            const std::string torn  = screenAt(c, 40.10, true, false, true);
            const std::string after = screenAt(c, 40.50, false, false, true);
            const std::string again = screenAt(c, 40.60, true, false, true);    // right after: no wait
            const bool tear = torn != plain && after == plain && again != plain;

            // Danger modal opening: the content under it jolts; the modal's own
            // cells (its frame, drawn in the overlay grid) stay where they are.
            ui::Context m2;
            screenAt(m2, 60.00, false, false, true);
            const std::string opening = screenAt(m2, 60.05, false, true, true);
            const std::string open2   = screenAt(m2, 60.60, false, true, true);  // settled, still open
            bool frameKept = true;
            {
                // Rows of the modal frame: compare the overlay cells of both frames.
                std::vector<std::string> a, b;
                size_t p = 0, q = 0;
                while (p < opening.size()) { size_t e = opening.find('\n', p); a.push_back(opening.substr(p, e - p)); p = e + 1; }
                while (q < open2.size())   { size_t e = open2.find('\n', q);   b.push_back(open2.substr(q, e - q));   q = e + 1; }
                const int mx = (cb.width() - 20) / 2, my = (cb.height() - 6) / 2;
                for (int y = my; y < my + 6 && y < (int)a.size(); ++y) {
                    std::vector<uint32_t> ca = Utf8Decode(a[(size_t)y].c_str()), cbv = Utf8Decode(b[(size_t)y].c_str());
                    for (int x = mx; x < mx + 20; ++x)
                        if (ca[(size_t)x] != cbv[(size_t)x]) frameKept = false;
                }
            }
            const bool jolt = opening != open2 && frameKept;

            ui::Context o;
            screenAt(o, 70.00, false, false, false);
            const bool off = screenAt(o, 70.10, true, false, false) == plain;
            check(tear && jolt && off,
                  "screen glitch: a fault tears everything, a danger modal jolts what is under it, "
                  "the next free as soon as one ends, none when switched off");
        }

        // Sidebar: tinted icon sprites (active in accent), a click picks, ↑/↓
        // on the strip the click focused move, every icon has its tooltip.
        {
            ui::Context c;
            int active = 0, changes = 0;
            static const ui::SidebarItem items[3] = { { 0, "SecOne" }, { 1, "SecTwo" }, { 2, "SecThree" } };
            auto body = [&](ui::Context& cc) {
                if (ui::Sidebar(cc, "sb", ui::Rect{ 0, 0, ui::SIDEBAR_W, 12 }, items, 3, active, 20))
                    ++changes;
            };
            run(c, ui::Input{}, body);
            bool looks = cb.sprites().size() == 3 && cb.sprites()[0].tint == SIDEBAR_ACTIVE &&
                         cb.sprites()[1].tint == SIDEBAR_ICON && cb.sprites()[2].tint == SIDEBAR_ICON;
            // the ground is the sidebar's own slot
            bool ground = false;
            for (const Shape& sh : cb.shapes()) ground = ground || sh.slot == SIDEBAR_BG;
            looks = looks && ground;
            // one row tall, centred in a two-row slot, on whole pixels
            looks = looks && cb.sprites()[1].h == 1.0f && cb.sprites()[1].y == 2.5f;
            run(c, press(1.5f, 4.5f), body);                           // slot 2: rows 4..5
            run(c, release(1.5f, 4.5f), body);
            const bool clicked = active == 2 && changes == 1;
            run(c, key([](ui::Input& i) { i.key_up = true; }), body);
            const bool keyed = active == 1;
            ui::Input h = at(1.5f, 0.5f);
            h.time = 20.0; run(c, h, body);
            h.time = 20.7; run(c, h, body);
            bool tip = false;
            for (int y = 0; y < cb.height(); ++y) tip = tip || topRow(cb, y).find("SecOne") != std::string::npos;
            check(looks && clicked && keyed && tip,
                  "sidebar: tinted icons, a click picks, ↑/↓ move, every icon has a tooltip");

            // The pointer answers to the icon, not to the two-row slot
            // around it — a click in the slot's corner is not on anything.
            active = 0; changes = 0;
            run(c, press(0.2f, 3.9f), body);              // slot 1, well off its icon
            run(c, release(0.2f, 3.9f), body);
            const bool corner = active == 0 && changes == 0;
            run(c, press(1.5f, 2.6f), body);              // the icon itself
            run(c, release(1.5f, 2.6f), body);
            check(corner && active == 1 && changes == 1,
                  "sidebar: the pointer answers to the icon, not to the slot around it");
        }

        // Tabs: a click switches, ←/→ on the focused strip switch.
        {
            ui::Context c;
            int active = 0;
            static const char* labels[] = { "ONE", "TWO", "THREE" };
            auto body = [&](ui::Context& cc) {
                ui::BeginTabs(cc, "tabs", ui::Rect{ 0, 0, 40, 8 }, labels, 3, active);
                ui::EndTabs(cc);
            };
            run(c, ui::Input{}, body);
            const std::string strip = topRow(cb, 0), hang = topRow(cb, 1);
            // "┌[ ONE ]─┐ TWO ├─┐ THREE ├──…"  and the bottoms hanging below
            const bool looks = strip.find("[ ONE ]") != std::string::npos &&
                               strip.find("┐ TWO ├") != std::string::npos &&
                               hang.find("└─────┘") != std::string::npos;
            run(c, press(12.0f, 0.5f), body);                          // "TWO"
            run(c, release(12.0f, 0.5f), body);
            const bool clicked = active == 1;
            run(c, key([](ui::Input& i) { i.key_right = true; }), body);
            check(looks && clicked && active == 2,
                  "tabs: inactive tabs break out of the frame; click and ←/→ switch");
        }
#ifdef CONUI_PLAYGROUND   // the theme editor is the playground's second window

        // ── the theme editor (5.15) ─────────────────────────────────────────
        {
            CellBuffer eb;
            eb.resize(60, 34, BG_WINDOW);
            ThemeStore ts;
            ts.init("");
            ThemeEditorState es;
            ui::Layout el;
            ui::Context ec;
            EditorResult er;
            auto edFrame = [&](const ui::Input& in) {
                eb.clear(BG_WINDOW);
                ec.begin(eb, in);
                er = BuildThemeEditor(ec, el, es, ts, 0, 2);
                ec.end();
            };
            auto find = [&](const char* s, int& fx, int& fy) {
                for (int y = 0; y < eb.height(); ++y) {
                    std::string row;
                    for (int x = 0; x < eb.width(); ++x) Utf8Append(row, eb.top(x, y).cp);
                    const size_t p = row.find(s);
                    if (p != std::string::npos) {
                        fx = Utf8Width(row.substr(0, p).c_str());
                        fy = y;
                        return true;
                    }
                }
                return false;
            };
            edFrame(ui::Input{});
            edFrame(ui::Input{});
            const Rgba before = ts.at(0).c[BG_PANEL];

            // Drag the R slider of the selected slot (bg.panel) to its far end.
            int rx = 0, ry = 0;
            // A slider is all geometry (§8.4) — no text to find it by — so it
            // is found by its label: "R", three cells wide, then the track.
            const bool hasR = find(" R   ", rx, ry);
            const int sx = rx + 1 + 3, sy = ry;
            ui::Input p; p.mouse_x = 58.0f; p.mouse_y = sy + 0.5f;
            p.mouse_down = p.mouse_pressed = true;
            // press on the track, drag to the right end
            ui::Input p0 = p; p0.mouse_x = sx + 2.5f;
            edFrame(p0);
            ui::Input h = p; h.mouse_pressed = false;
            edFrame(h);
            ui::Input rl = h; rl.mouse_down = false; rl.mouse_released = true;
            edFrame(rl);
            const bool live = ts.at(0).c[BG_PANEL].r == 255 && ts.at(0).c[BG_PANEL].g == before.g;
            const bool counted = ts.changedSlots(0) == 1 && ts.modified(0);
            ts.revert(0);
            const bool reverted = ts.at(0).c[BG_PANEL].r == before.r && !ts.modified(0);
            check(hasR && live && counted && reverted,
                  "theme editor: a slider edits the theme live, it counts as modified, REVERT undoes it");

            // HEX: a wrong string is shown as an error and NOT applied; a right one is.
            int hx = 0, hy = 0;
            find("HEX   #", hx, hy);                    // the field, not the list's column header
            edFrame(press(hx + 7.0f, hy + 0.5f));
            edFrame(release(hx + 7.0f, hy + 0.5f));
            edFrame(key([](ui::Input& i) { i.key_select_all = true; }));
            edFrame(typed("#12zz"));
            // The flag is computed from the text as the frame starts — like
            // hover, it shows one frame after the keystroke.
            edFrame(ui::Input{});
            const bool rejected = ts.at(0).c[BG_PANEL].r == before.r;
            int ex = 0, ey = 0;
            const bool errShown = find("✗", ex, ey) && ey == hy;
            edFrame(key([](ui::Input& i) { i.key_select_all = true; }));
            edFrame(typed("#112233"));
            const Rgba& now = ts.at(0).c[BG_PANEL];
            check(rejected && errShown && now.r == 0x11 && now.g == 0x22 && now.b == 0x33,
                  "theme editor: a bad HEX is flagged and not applied, a good one is");

            // Esc in the HEX field stays in the field; Esc elsewhere closes the editor.
            edFrame(key([](ui::Input& i) { i.key_escape = true; }));
            const bool keptByField = !er.close;
            ec.setFocus(ui::kNoId);
            edFrame(key([](ui::Input& i) { i.key_escape = true; }));
            check(keptByField && er.close, "theme editor: Esc closes it, but not from inside a field");

            // NEW + SAVE, on a scratch copy of themes/ so the real one is untouched.
            // A scratch themes/ of its own, cleared first: a second run must
            // not find the first run's "Phosphor 2" and name its copy "3".
            std::error_code fsErr;
            std::filesystem::remove_all(dir + "selftest-themes/", fsErr);
            ThemeStore disk;
            disk.init(dir + "selftest-themes/");
            const int n0 = disk.count();
            const int copy = disk.duplicate(0);
            const bool fresh = disk.count() == n0 + 1 && disk.modified(copy) &&
                               disk.at(copy).name == disk.at(0).name + " 2";
            std::string err;
            const bool saved = disk.saveAt(copy, err) && !disk.modified(copy);
            ThemeStore again;
            again.init(dir + "selftest-themes/");
            const bool reread = again.indexOf((disk.at(0).name + " 2").c_str()) >= 0;
            check(fresh && saved && reread,
                  "theme editor: NEW duplicates, SAVE writes it and it loads back from disk");
            if (!saved) std::printf("      save: %s\n", err.c_str());
        }
#endif
    }


    // ── 11. data.bin ──────────────────────────────────────────────
    // Encoding only: the file itself is the user's, and a test must not
    // overwrite it.
    {
        AppState a;
        a.theme = "Midnight";
        a.locale = "ru";
        a.opacity = 0.5f;
        a.backdrop = Backdrop::Mica;
        a.density = Density::Large;
        a.main_window = WindowGeometry{ 120.0f, -900.0f, 1116.0f, 820.0f, 1 };  // left monitor, maximised
        a.theme_editor = WindowGeometry{ 40.0f, 60.0f, 540.0f, 680.0f, 0 };
        a.theme_editor_open = 0;
        a.saved_gs = 1.5f;
        a.layout = "ws.main=0.3100;ws.left=0.7000";
        a.app_data = "{\"screen\":2,\"notes\":\"Привет\\n\"}";
        a.panel_opacity = 0.35f;
        a.glitches = 0;
        AppState b;
        const bool ok = DeserializeAppState(SerializeAppState(a), b);
        check(ok && b.theme == a.theme && b.locale == a.locale && b.opacity == a.opacity &&
              b.backdrop == a.backdrop && b.density == a.density &&
              b.main_window.x == 120.0f && b.main_window.y == -900.0f &&
              b.main_window.maximized == 1 && b.theme_editor.w == 540.0f &&
              b.theme_editor_open == 0 && b.saved_gs == 1.5f &&
              b.layout == a.layout && b.app_data == a.app_data &&
              b.panel_opacity == a.panel_opacity && b.glitches == 0,
              "data.bin round-trips every field, window geometry included");

        // An older, shorter file: everything it has is read, the rest keeps
        // its default. Cut the buffer just before app_data (and so also the
        // panel_opacity and glitches written after it).
        std::vector<char> buf = SerializeAppState(a);
        buf.resize(buf.size() - sizeof(uint8_t) - sizeof(float) - 4 - a.app_data.size());
        AppState c;
        const bool shortOk = DeserializeAppState(buf, c);
        check(shortOk && c.layout == a.layout && c.app_data.empty() && c.theme == "Midnight" &&
              c.panel_opacity == 1.0f && c.glitches == 1,
              "an older, shorter data.bin loads; the missing tail keeps its defaults");

        // A foreign file: rejected, and nothing in the state is touched.
        std::vector<char> alien = SerializeAppState(a);
        alien[0] ^= 0x5A;
        AppState d;
        const bool alienRejected = !DeserializeAppState(alien, d) && d.theme == "Phosphor";
        // Damaged values: a density of 7 and an opacity of NaN keep the defaults.
        AppState e = a;
        e.density = (Density)7;
        e.opacity = std::nanf("");
        e.panel_opacity = 3.0f;
        e.glitches = 7;
        AppState f;
        DeserializeAppState(SerializeAppState(e), f);
        check(alienRejected && f.density == Density::Normal && f.opacity == AppState{}.opacity &&
              f.panel_opacity == 1.0f && f.glitches == 1 && f.backdrop == Backdrop::Mica,
              "a foreign file is refused; out-of-range values fall back, the rest still loads");

        // Tiny was appended to Density (3, after Large): it must load back,
        // and its cell must be smaller than Compact's.
        AppState g = a;
        g.density = Density::Tiny;
        AppState h;
        const bool tinyLoads = DeserializeAppState(SerializeAppState(g), h) && h.density == Density::Tiny;
        check(tinyLoads && FontPxForDensity(Density::Tiny) < FontPxForDensity(Density::Compact),
              "the Tiny density round-trips through data.bin and is below Compact");
    }

#if defined(CONUI_PLAYGROUND) && defined(CONUI_ABOUT_CAT)
    // The ABOUT cat, at its size in the box: facing the viewer, two ring eyes; in
    // profile either way, none; an outline all round, the halftone outside.
    {
        auto count = [](const std::vector<aboutcat::Pix>& px, aboutcat::Tone t, char ch) {
            int n = 0;
            for (const aboutcat::Pix& p : px) n += p.tone == t && (ch == 0 || p.ch == ch);
            return n;
        };
        std::vector<aboutcat::Pix> front, left, right;
        aboutcat::Render(26, 9, 9.0f / 20.0f, 0.0f, front);
        aboutcat::Render(26, 9, 9.0f / 20.0f, 1.5707963f, left);
        aboutcat::Render(26, 9, 9.0f / 20.0f, -1.5707963f, right);
        check(count(front, aboutcat::TONE_EYE, '(') == 2 && count(front, aboutcat::TONE_EYE, ')') == 2,
              "about cat: two ring eyes facing the viewer");
        check(count(left, aboutcat::TONE_EYE, 0) == 0 && count(right, aboutcat::TONE_EYE, 0) == 0,
              "about cat: no eyes in profile");
        std::vector<aboutcat::Pix> shut;
        aboutcat::Render(26, 9, 9.0f / 20.0f, 0.0f, shut, 1.0f);
        check(count(shut, aboutcat::TONE_EYE, '(') == 0 && count(shut, aboutcat::TONE_EYE, '-') >= 4,
              "about cat: a blink shuts both eyes to a line");
        check(count(front, aboutcat::TONE_LINE, 0) > 25 && count(left, aboutcat::TONE_LINE, 0) > 15 &&
              count(right, aboutcat::TONE_LINE, 0) > 15 && count(front, aboutcat::TONE_DOTS, '.') > 25,
              "about cat: outlined, on a halftone");

        // The layer: one sprite over the card, nothing in its cells; the next
        // frame redraws the same picture in place, and only its box is dirty.
        RasterStore rs;
        rs.init(512, 512);
        CellBuffer lb;
        lb.resize(40, 14, BG_WINDOW);
        ui::Context lc;
        const Theme& th = BuiltinTheme(0);
        auto frameAt = [&](double t) {
            lb.clear(BG_WINDOW);
            ui::Input in;
            in.time = t;
            lc.begin(lb, in);
            DrawAboutCat(lc, &rs, &th, ui::Rect{ 2, 2, 30, 10 }, 0.25f, t, false);
            lc.end();
        };
        frameAt(0.0);
        const int added = rs.count();
        rs.clearDirty();
        frameAt(1.0);
        bool blank = true;
        for (int y = 2; y < 12; ++y)
            for (int x = 2; x < 32; ++x) blank = blank && lb.at(x, y).cp == ' ';
        const bool one = lb.sprites().size() == 1 && added == 1 && rs.count() == 1;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        rs.dirtyRect(x0, y0, x1, y1);
        const RasterInfo& ri = rs.get(0);
        check(one && blank && rs.dirty() && !rs.dirtyAll() &&
              x0 == ri.ax && y0 == ri.ay && x1 - x0 == ri.aw && y1 - y0 == ri.ah,
              "about cat layer: one sprite, redrawn in place, only its box sent");
        // A quarter of the cell: 120 x 40 characters, painted 9 x 20 px and
        // averaged down 4 x 4, in the 30 x 10 card (270 x 200 px).
        int lit = 0;
        const Rgba& text = th.c[TEXT];
        auto within3 = [](int a, int b) { return a - b <= 3 && b - a <= 3; };
        for (int y = 0; y < ri.ah; ++y)
            for (int x = 0; x < ri.aw; ++x) {
                const uint8_t* p = rs.pixels() + ((size_t)(ri.ay + y) * rs.texW() + ri.ax + x) * 4;
                lit += p[3] > 64 && within3(p[0], text.r) && within3(p[1], text.g) && within3(p[2], text.b);
            }
        std::printf("      about cat layer: %d x %d px, %d outline px\n", ri.aw, ri.ah, lit);
        check(ri.aw == 270 && ri.ah == 200 && lit > 300,
              "about cat layer: four times the detail, the outline in the theme's text colour");

        // Between two phases the picture is both, blended: halfway, it
        // differs from each of them.
        auto snap = [&]() {
            std::vector<uint8_t> px((size_t)ri.aw * ri.ah * 4);
            for (int y = 0; y < ri.ah; ++y)
                std::memcpy(&px[(size_t)y * ri.aw * 4],
                            rs.pixels() + ((size_t)(ri.ay + y) * rs.texW() + ri.ax) * 4, (size_t)ri.aw * 4);
            return px;
        };
        frameAt(10.0 / 30.0);
        const std::vector<uint8_t> a = snap();
        frameAt(11.0 / 30.0);
        const std::vector<uint8_t> b = snap();
        frameAt(10.5 / 30.0);
        const std::vector<uint8_t> mid = snap();
        check(a != b && mid != a && mid != b, "about cat layer: the swing blends one phase into the next");

        // The cost of a frame once its phases are traced: painting only.
        // The median of single frames: the machine's other work skews a mean.
        std::vector<double> times;
        for (int k = 0; k < 21; ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            frameAt((10.0 + k * 0.045) / 30.0);
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::nth_element(times.begin(), times.begin() + 10, times.end());
        std::printf("      about cat layer: %.2f ms a frame (median), phases traced\n", times[10]);
    }
#endif

#ifdef CONUI_PLAYGROUND
    // The playground's own part of data.bin: JSON, tolerant both ways.
    {
        PlaygroundState a;
        a.screen = 2; a.tab = 1;
        a.name = "мой проект"; a.search = "tree"; a.port = "8080";
        a.history = { "one", "two" };
        a.notes = "";                                   // cleared by the user
        a.grid = true; a.autosave = 0; a.steps = 5; a.range_lo = 20; a.range_hi = 30;
        a.sort_col = 1; a.sort_desc = false; a.show_hidden = true; a.sort_by = 2;
        PlaygroundState b;
        LoadPlaygroundState(b, SavePlaygroundState(a));
        const bool round = b.screen == 2 && b.tab == 1 && b.name == a.name && b.port == "8080" &&
                           b.history.size() == 2 && b.history[1] == "two" &&
                           b.notes.empty() && b.notes_loaded && b.grid && b.autosave == 0 &&
                           b.steps == 5.0f && b.range_lo == 20.0f && b.range_hi == 30.0f &&
                           b.sort_col == 1 && !b.sort_desc && b.show_hidden && b.sort_by == 2;
        // Cleared notes stay cleared: the first frame must not refill them.
        {
            CellBuffer cb2;
            cb2.resize(124, 41, BG_WINDOW);
            ui::Context c;
            ThemeStore ts;
            ts.init("");
            AppState st;
            ui::Layout lay;
            RasterStore rs;
            AppServices s{ ts, st, lay, rs, 0, 60.0, 0, false };
            AppActions act;
            c.begin(cb2, ui::Input{});
            BuildPlayground(c, s, act, b, -1);
            c.end();
        }
        check(round && b.notes.empty(),
              "playground state round-trips through JSON; cleared notes stay cleared");

        PlaygroundState d;
        LoadPlaygroundState(d, "{\"screen\": 9, \"tab\": -1, \"steps\": 1e300, \"future_key\": 1,"
                               " \"range_lo\": 80, \"range_hi\": 10}");
        PlaygroundState e;
        LoadPlaygroundState(e, "{\"screen\": 1, \"name\": ");       // cut off mid-file
        check(d.screen == 0 && d.tab == 0 && d.steps == 3.0f && d.range_lo == 12.0f &&
              e.screen == 0 && e.name == "conui_playground",
              "playground state: bad values and a damaged file fall back to defaults");
    }
#endif

    std::printf(g_fail ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", g_fail);
    return g_fail;
}
