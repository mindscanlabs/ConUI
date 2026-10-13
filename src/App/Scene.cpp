#include "Scene.hpp"
#include "../Core/RasterStore.hpp"
#include "../Render/CellBuffer.hpp"
#include "../UI/Glyphs.hpp"
#include "../UI/Theme.hpp"
#include "../UI/WindowChrome.hpp"
#include "../UI/Widgets/Widgets.hpp"   // DrawFrame, DrawFrameTitle

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// The cell is 9 × 20, so a square in pixels is NOT a square in cells. Shapes
// are declared in cell fractions (§2.1): a bar as thick across as it is
// along converts through this.
const float kCellAspect = 9.0f / 20.0f;

// ---- widgets that live in the shape layer (§2.1) --------------------------
//
// These are minimal stand-ins for the real Widgets/; what they
// exist to prove here is that the layer underneath them works.

const float kBarH = 0.62f, kBarR = 0.14f;

void progress(CellBuffer& cb, int x, int y, int w, float v, uint8_t on, uint8_t off) {
    const float y0 = y + (1.0f - kBarH) * 0.5f;
    cb.shape((float)x, y0, (float)w, kBarH, off, kBarR);
    if (v > 0.0f) {
        const float f = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        cb.shape((float)x, y0, w * f, kBarH, on, kBarR);
    }
}

void scrollbar(CellBuffer& cb, int x, int y, int h, float pos, float frac, uint8_t thumb) {
    const float pad = 0.30f, minLen = 1.20f, wcells = 0.55f;
    const float run = h - 2 * pad;
    float len = run * frac;
    if (len < minLen) len = minLen;
    cb.shape(x + (1.0f - wcells) * 0.5f, y + pad + (run - len) * pos,
             wcells, len, thumb, 0.5f, 0.80f);
}

void splitterV(CellBuffer& cb, int x, int y, int h, bool hot) {
    if (hot) {
        cb.shape(x + 0.34f, y + 0.15f, 0.32f, h - 0.30f, ACCENT, 0.5f, 0.85f);
        const float cy = y + h * 0.5f;
        for (int i = -1; i <= 1; ++i)
            cb.shape(x + 0.28f, cy + i * 0.55f - 0.09f, 0.44f, 0.18f, TEXT_INV, 0.5f, 0.9f);
    } else {
        cb.shape(x + 0.44f, y + 0.5f, 0.12f, h - 1.0f, BORDER_DIM, 0.5f);
    }
}

void splitterH(CellBuffer& cb, int x, int y, int w) {
    const float t = 0.12f * kCellAspect;
    cb.shape(x + 1.0f, y + (1.0f - t) * 0.5f, w - 2.0f, t, BORDER_DIM, 0.5f);
}

// Status bar under an edge-to-edge rule. Rows H-3/H-2/H-1 are the rule, the
// bar and a margin — the same bottom as the playground.
void drawStatus(CellBuffer& cb, int W, int H, const char* badge, const char* hint) {
    if (H < 4) return;
    for (int i = 0; i < W; ++i) cb.put(i, H - 3, Gly::H, BORDER_DIM, BG_WINDOW);
    int sx = 2;
    char b[32];
    std::snprintf(b, sizeof b, " %s ", badge);
    sx += cb.text(sx, H - 2, b, TEXT_INV, BG_SEL, ATTR_BOLD);
    if (Utf8Width(hint) + sx + 4 < W) cb.textRight(W - 3, H - 2, hint, TEXT_MUTED, BG_WINDOW);
}

} // namespace

// A stand-in photograph: smooth gradients and a soft disc, things the glyph
// vocabulary could never produce, so there is no doubt it came from the raster
// layer rather than from the cell grid (§16).
int MakeDemoRaster(RasterStore& store) {
    const int W = 108, H = 80;
    std::vector<uint8_t> px((size_t)W * H * 4);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float t = (float)y / H;
            float R = 18 + 210 * std::pow(1.0f - t, 2.2f) * 0.75f;
            float G = 26 + 130 * std::pow(1.0f - t, 1.6f);
            float B = 54 + 150 * (0.35f + 0.65f * t);
            const float dx = x - 74.0f, dy = y - 26.0f;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float sun  = std::fmin(std::fmax(1.0f - (d - 9.0f) / 3.0f, 0.0f), 1.0f);
            const float halo = std::fmin(std::fmax(1.0f - d / 40.0f, 0.0f), 1.0f);
            R += 90 * halo * halo + 140 * sun;
            G += 60 * halo * halo + 120 * sun;
            B += 10 * halo * halo +  70 * sun;
            const float h1 = 52 + 16 * std::sin(x * 0.075f) + 6 * std::sin(x * 0.21f + 1.3f);
            const float h2 = 62 + 10 * std::sin(x * 0.055f + 2.1f);
            if (y > h1) { R = R * 0.30f + 14; G = G * 0.32f + 20; B = B * 0.40f + 34; }
            if (y > h2) { R = R * 0.55f + 6;  G = G * 0.58f + 10; B = B * 0.62f + 18; }
            uint8_t* p = &px[((size_t)y * W + x) * 4];
            p[0] = (uint8_t)std::fmin(std::fmax(R, 0.0f), 255.0f);
            p[1] = (uint8_t)std::fmin(std::fmax(G, 0.0f), 255.0f);
            p[2] = (uint8_t)std::fmin(std::fmax(B, 0.0f), 255.0f);
            p[3] = 255;
        }
    }
    return store.addRGBA(px.data(), W, H);
}

void BuildSelfTestScene(CellBuffer& cb, int rasterId) {
    const int W = 76, H = 28;
    cb.resize(W, H, BG_WINDOW);

    // Row 0 is the caption and nothing else; row 1 stays clear so the content
    // cannot look tucked under it (§8.1).
    chrome::DrawCaption(cb, "CONUI · RENDER CORE", /*focused=*/true, 9, 20);

    // ---- left column: the glyph checks -------------------------
    ui::DrawFrame(cb, ui::Rect{ 1, 2, 34, 11 }, BORDER, BG_PANEL);
    ui::DrawFrameTitle(cb, 1, 2, "JOINTS", BORDER, BG_PANEL, ACCENT);
    cb.put(1,  6, Gly::TEE_R, BORDER, BG_PANEL);
    cb.put(34, 6, Gly::TEE_L, BORDER, BG_PANEL);
    for (int i = 2; i < 34; ++i) cb.put(i, 6, Gly::H, BORDER_DIM, BG_PANEL);
    cb.put(18, 6, Gly::CROSS, BORDER_DIM, BG_PANEL);
    for (int j = 3; j < 6; ++j)  cb.put(18, j, Gly::V, BORDER_DIM, BG_PANEL);
    for (int j = 7; j < 12; ++j) cb.put(18, j, Gly::V, BORDER_DIM, BG_PANEL);
    cb.put(18, 2,  Gly::TEE_D, BORDER, BG_PANEL);
    cb.put(18, 12, Gly::TEE_U, BORDER, BG_PANEL);
    cb.text(3,  4, "top-left",    TEXT,  BG_PANEL);
    cb.text(20, 4, "top-right",   TEXT,  BG_PANEL);
    cb.text(3,  8, "bottom-left", TEXT2, BG_PANEL);
    cb.text(20, 8, "bot-right",   TEXT2, BG_PANEL);
    cb.text(3, 10, "eighths", TEXT_MUTED, BG_PANEL);
    for (int i = 1; i <= 8; ++i) cb.put(11 + i - 1, 10, Gly::LeftEighths(i),  TEXT,   BG_PANEL);
    for (int i = 1; i <= 8; ++i) cb.put(21 + i - 1, 10, Gly::LowerEighths(i), ACCENT, BG_PANEL);

    splitterH(cb, 1, 13, 34);

    // ---- left column, lower: everything that is NOT a cell ----------------
    ui::DrawFrame(cb, ui::Rect{ 1, 14, 34, 10 }, BORDER, BG_PANEL);
    ui::DrawFrameTitle(cb, 1, 14, "SHAPE LAYER", BORDER, BG_PANEL, ACCENT);

    cb.text(3, 16, "SMOOTH", TEXT2, BG_PANEL);
    progress(cb, 11, 16, 18, 0.641f, ACCENT, DITHER);
    cb.textRight(32, 16, "64%", TEXT, BG_PANEL, ATTR_BOLD);
    cb.text(3, 17, "FINE",   TEXT2, BG_PANEL);
    progress(cb, 11, 17, 18, 0.017f, SUCCESS, DITHER);
    cb.textRight(32, 17, "1.7%", TEXT, BG_PANEL, ATTR_BOLD);
    cb.text(3, 18, "FULL",   TEXT2, BG_PANEL);
    progress(cb, 11, 18, 18, 1.000f, SUCCESS, DITHER);
    cb.textRight(32, 18, "100%", TEXT, BG_PANEL, ATTR_BOLD);

    cb.text(3, 20, "SCROLL", TEXT2, BG_PANEL);
    cb.text(11, 20, "no arrows/track", TEXT_MUTED, BG_PANEL);
    scrollbar(cb, 33, 15, 8, 0.25f, 0.45f, ACCENT_DIM);

    cb.text(3, 22, "RASTER", TEXT2, BG_PANEL);
    if (rasterId >= 0) {
        cb.shape(11.0f - 1.0f / 9.0f, 21.0f - 1.0f / 20.0f,
                 6.0f + 2.0f / 9.0f, 2.0f + 2.0f / 20.0f, BORDER_DIM, 0.0f, 1.0f, 1.0f);
        cb.sprite(11.0f, 21.0f, 6.0f, 2.0f, (uint16_t)rasterId);
        cb.text(19, 22, "108×80 RGBA", TEXT_MUTED, BG_PANEL);
    } else {
        cb.text(11, 22, "(no raster store)", TEXT_MUTED, BG_PANEL);
    }

    splitterV(cb, 35, 2, 22, /*hot=*/true);

    // ---- right column: the seam checks, straight on the window background --
    cb.text(37, 2, "SOLID (no seams)", TEXT_MUTED, BG_WINDOW);
    cb.fill(37, 3, 32, 3, Gly::FULL, ACCENT, BG_WINDOW);

    cb.text(37, 7, "SHADES ░▒▓█", TEXT_MUTED, BG_WINDOW);
    const uint32_t shades[4] = { Gly::SHADE_25, Gly::SHADE_50, Gly::SHADE_75, Gly::FULL };
    for (int b = 0; b < 4; ++b) cb.fill(37 + b * 8, 8, 8, 3, shades[b], TEXT, BG_WINDOW);

    // inverted row — selection is a colour swap, not a highlight (§1)
    cb.bgRect(37, 12, 37, 1, BG_SEL);
    cb.text(38, 12, "selected row — inverted, full width", TEXT_INV, BG_SEL, ATTR_BOLD);

    cb.text(37, 14, "gjpqy 0O1lI {}[]()<> ✓ ✗ ● ○ ◆ ▲ ▼", TEXT,  BG_WINDOW);
    cb.text(37, 15, "Строка на кириллице — выносные", TEXT2, BG_WINDOW);
    cb.text(37, 16, "bold via 1px offset:", TEXT_MUTED, BG_WINDOW);
    cb.text(58, 16, "ConUI", TEXT, BG_WINDOW, ATTR_BOLD);

    // An overlay, so the band ordering is exercised: the splitter and the
    // scrollbar must NOT show through this panel.
    cb.setBand(Z_OVERLAY);
    ui::DrawFrame(cb, ui::Rect{ 30, 19, 30, 5 }, DANGER, BG_PANEL);
    ui::DrawFrameTitle(cb, 30, 19, "OVERLAY", DANGER, BG_PANEL, DANGER);
    cb.text(32, 21, "shapes must not bleed through", TEXT, BG_PANEL);
    cb.setBand(Z_CONTENT);

    // status bar, under a rule that runs edge to edge
    for (int i = 0; i < W; ++i) cb.put(i, H - 3, Gly::H, BORDER_DIM, BG_WINDOW);
    int sx = 2;
    sx += cb.text(sx, H - 2, " STAGE 2b ", TEXT_INV, BG_SEL, ATTR_BOLD);
    sx += 2;
    cb.text(sx, H - 2, "layers · shapes · raster", TEXT2, BG_WINDOW);
    cb.textRight(W - 3, H - 2, "1 draw call ◆", ACCENT_DIM, BG_WINDOW);
}

// ---------------------------------------------------------------------------
// The live body of a real window. The window resizes, so nothing
// here may assume a size: every panel is derived from the grid it is given,
// and anything that will not fit is dropped rather than drawn off the edge.
// The applications in src/App/ replace this with their own frames.
// ---------------------------------------------------------------------------
void BuildStageScene(CellBuffer& cb, int top, int rasterId, bool compact) {
    const int W = cb.width(), H = cb.height();
    if (W < 24 || H < top + 8) {
        // Too small to lay anything out: say so instead of drawing rubbish.
        if (H > top) cb.text(1, top, "window too small", TEXT_MUTED, BG_WINDOW);
        return;
    }

    // ---- menu row, then a rule that runs edge to edge --------------------
    int y = top;
    if (!compact) {
        int mx = 2;
        const char* menus[] = { "FILE", "VIEW", "WIDGETS", "THEME", "HELP" };
        for (int i = 0; i < 5; ++i) {
            const int len = Utf8Width(menus[i]);
            if (mx + len + 2 >= W - 2) break;
            if (i == 3) {
                cb.put(mx++, y, ' ', TEXT, BG_WINDOW);
                mx += cb.text(mx, y, menus[i], TEXT_INV, BG_SEL, ATTR_BOLD);
                cb.put(mx++, y, ' ', TEXT, BG_WINDOW);
            } else {
                mx += cb.text(mx, y, menus[i], TEXT, BG_WINDOW);
            }
            mx += 3;
        }
    } else {
        int tx = 2;
        const char* tools[] = { "[ NEW ]", "[ OPEN ]", "[ SAVE ]", "[ REVERT ]" };
        for (int i = 0; i < 4; ++i) {
            const int len = Utf8Width(tools[i]);
            if (tx + len >= W - 2) break;
            tx += cb.text(tx, y, tools[i], i == 2 ? ACCENT : TEXT, BG_WINDOW) + 1;
        }
    }
    {
        char right[96];
        std::snprintf(right, sizeof right, "PHOSPHOR · %d×%d", W, H);
        if (Utf8Width(right) + 24 < W) cb.textRight(W - 2, y, right, TEXT_MUTED, BG_WINDOW);
    }
    ++y;
    chrome::DrawRule(cb, y);
    ++y;

    // ---- body: two columns with a draggable seam between them ------------
    // Rows H-3, H-2 and H-1 are the rule, the status bar and a margin, so the
    // status bar does not sit flush against the system frame.
    const int bottom = H - 4;
    const int bh = bottom - y + 1;
    if (bh < 4) return;

    const int leftW = compact ? W - 2 : (W - 3) * 2 / 5;
    const int seam  = 1 + leftW;
    const int rightX = seam + 1;
    const int rightW = W - 1 - rightX;

    ui::DrawFrame(cb, ui::Rect{ 1, y, leftW, bh }, BORDER, BG_PANEL);
    ui::DrawFrameTitle(cb, 1, y, compact ? "SLOTS" : "CONTROLS", BORDER, BG_PANEL, ACCENT);

    int iy = y + 2;
    const int ix = 3;
    if (iy < bottom) {
        cb.text(ix, iy, "OPACITY", TEXT2, BG_PANEL);
        const int barX = ix + 9, barW = leftW - 9 - ix - 4;
        if (barW > 4) {
            progress(cb, barX, iy, barW, 0.88f, ACCENT, DITHER);
            cb.textRight(leftW - 1, iy, "88%", TEXT, BG_PANEL, ATTR_BOLD);
        }
        ++iy;
    }
    if (iy < bottom) {
        cb.text(ix, iy, "BUILD", TEXT2, BG_PANEL);
        const int barX = ix + 9, barW = leftW - 9 - ix - 4;
        if (barW > 4) {
            progress(cb, barX, iy, barW, 0.641f, SUCCESS, DITHER);
            cb.textRight(leftW - 1, iy, "64%", TEXT, BG_PANEL, ATTR_BOLD);
        }
        iy += 2;
    }
    if (iy < bottom && rasterId >= 0 && leftW > 16) {
        cb.text(ix, iy, "PREVIEW", TEXT2, BG_PANEL);
        if (iy + 2 <= bottom - 1) {
            const float rx = (float)ix, ry = (float)(iy + 1);
            cb.shape(rx - 1.0f / 9.0f, ry - 1.0f / 20.0f,
                     6.0f + 2.0f / 9.0f, 2.0f + 2.0f / 20.0f, BORDER_DIM, 0.0f, 1.0f, 1.0f);
            cb.sprite(rx, ry, 6.0f, 2.0f, (uint16_t)rasterId);
            if (leftW > 26) cb.text(ix + 8, iy + 1, "raster layer", TEXT_MUTED, BG_PANEL);
        }
    }
    // The panel's own scrollbar: one cell of layout, drawn half that wide.
    scrollbar(cb, leftW - 1, y + 1, bh - 2, 0.2f, 0.5f, ACCENT_DIM);

    if (compact) {
        drawStatus(cb, W, H, "EDITOR", "Ctrl+S save · Esc close");
        return;
    }

    // seam + right column
    splitterV(cb, seam, y, bh, /*hot=*/false);

    if (rightW > 12) {
        ui::DrawFrame(cb, ui::Rect{ rightX, y, rightW, bh }, BORDER, BG_PANEL);
        ui::DrawFrameTitle(cb, rightX, y, "OUTPUT", BORDER, BG_PANEL, ACCENT);
        const char* lines[] = {
            "native frame, own caption",
            "close box left, three right",
            "row 1 stays clear",
            "",
            "drag the caption to move",
            "drag any edge to resize",
            "F11 toggles fullscreen",
        };
        const int n = (int)(sizeof lines / sizeof *lines);
        for (int i = 0; i < n && y + 2 + i < bottom; ++i)
            cb.text(rightX + 2, y + 2 + i, lines[i],
                    lines[i][0] ? TEXT : TEXT_MUTED, BG_PANEL);
    }

    drawStatus(cb, W, H, "STAGE 3", "F11 fullscreen · Esc quit");
}

