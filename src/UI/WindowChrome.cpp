#include "WindowChrome.hpp"
#include "Glyphs.hpp"
#include "Theme.hpp"
#include "../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>

namespace {

// A caption box is square in PIXELS, and the cell is not square, so its width
// in cells depends on the cell aspect ratio. Everything below is expressed in
// cell fractions (§2.1) and converted once, here.
constexpr float kBoxH = 0.55f;          // 11 px in a 20 px row

// The caption bar is a third taller than one row, so its text and boxes get
// breathing room above and below instead of filling the row edge to edge
// (§8.1). The extra height bleeds down into the row that follows (always
// blank — CLEAR_ROW), never up past the window's own top edge.
constexpr float kBandExtra = 1.0f / 3.0f;
constexpr float kBandH     = 1.0f + kBandExtra;
constexpr float kPadTop    = kBandExtra * 0.5f;

inline float cellAspect(int cellW, int cellH) {
    return cellH > 0 ? (float)cellW / (float)cellH : 0.45f;
}
inline float boxW(int cellW, int cellH) { return kBoxH / cellAspect(cellW, cellH); }
inline float lineY(int cellH) { return cellH > 0 ? 1.0f / (float)cellH : 0.05f; }
inline float px(int n, int cellW) { return cellW > 0 ? (float)n / (float)cellW : 0.0f; }
inline float py(int n, int cellH) { return cellH > 0 ? (float)n / (float)cellH : 0.0f; }

/** The band's height in pixels, rounded — the single source of truth for
 *  both the platform layer's hit-testing and the band drawn below, so its
 *  edge always lands on a whole pixel. An edge that falls between pixels gets
 *  antialiased and looks translucent even at 100% opacity. */
inline int bandPx(int cellH) { return cellH > 0 ? (int)std::lround((float)cellH * kBandH) : cellH; }
inline float bandFrac(int cellH) { return cellH > 0 ? (float)bandPx(cellH) / (float)cellH : kBandH; }

/** Left edge of each box, in cells. Shared by the drawing and the hit test. */
void boxOrigins(int cols, int cellW, int cellH, float out[chrome::BTN_COUNT]) {
    const float bw = boxW(cellW, cellH);
    out[chrome::BTN_CLOSE]      = 1.0f;
    const float right = cols - 1.0f - bw * 3.0f - 0.8f;
    out[chrome::BTN_COLLAPSE]   = right;
    out[chrome::BTN_ZOOM]       = right + bw * 1.0f + 0.4f;
    out[chrome::BTN_FULLSCREEN] = right + bw * 2.0f + 0.8f;
}

void drawBox(CellBuffer& cb, float x, int kind, uint8_t ink, int cellW, int cellH,
             bool filled) {
    const float h = kBoxH, w = boxW(cellW, cellH);
    const float y = kPadTop + (1.0f - h) * 0.5f;
    const float L = lineY(cellH);

    // Pressed state fills the box instead of inventing a new colour — the
    // system's own rule: hierarchy by inversion, not by decoration (§9).
    cb.shape(x, y, w, h, ink, 0.0f, 1.0f, filled ? 0.0f : 1.0f);
    if (filled) return;

    switch (kind) {
    case chrome::BTN_CLOSE:
        break;                                   // an empty square, as in System 7
    case chrome::BTN_COLLAPSE:                   // a line across the middle
        cb.shape(x + px(2, cellW), y + h * 0.5f - L * 0.5f,
                 w - px(4, cellW), L, ink);
        break;
    case chrome::BTN_ZOOM:                       // a smaller square in the corner
        cb.shape(x + px(2, cellW), y + py(2, cellH), w * 0.45f, h * 0.45f,
                 ink, 0.0f, 1.0f, 1.0f);
        break;
    case chrome::BTN_FULLSCREEN:                 // two opposed corner marks
        cb.shape(x + px(2, cellW), y + py(2, cellH), w * 0.34f, L, ink);
        cb.shape(x + px(2, cellW), y + py(2, cellH), px(1, cellW), h * 0.34f, ink);
        cb.shape(x + w - px(2, cellW) - w * 0.34f, y + h - py(3, cellH),
                 w * 0.34f, L, ink);
        cb.shape(x + w - px(3, cellW), y + h - py(2, cellH) - h * 0.34f,
                 px(1, cellW), h * 0.34f, ink);
        break;
    default:
        break;
    }
}

} // namespace

namespace chrome {

void DrawCaption(CellBuffer& cb, const char* title, bool focused,
                 int cellW, int cellH, int hover, int pressed, int spillPx,
                 const CaptionIcons* icons) {
    const int cols = cb.width();
    if (cols <= 0 || cellW <= 0 || cellH <= 0) return;

    // A shape, not a surface: surfaces are whole cells, and this band is a
    // third taller than one. It also covers the spill — the
    // leftover pixels short of a whole cell at the right edge — so it always
    // reaches the true edge of the window, not just the last whole column.
    const float band = bandFrac(cellH);
    const float w = (float)cols + (float)spillPx / (float)cellW;
    cb.shape(0.0f, 0.0f, w, band, BG_RAISED);

    const uint8_t ink = focused ? BORDER : TEXT_MUTED;
    float ox[BTN_COUNT];
    boxOrigins(cols, cellW, cellH, ox);

    if (focused) {
        const bool raster = icons && icons->valid();
        for (int b = 0; b < BTN_COUNT; ++b) {
            const uint8_t c = (b == BTN_CLOSE) ? (uint8_t)DANGER : ink;
            const bool    f = (b == pressed);
            const uint8_t shown = (b == hover && b != pressed) ? (uint8_t)ACCENT : c;
            if (!raster) { drawBox(cb, ox[b], b, f ? c : shown, cellW, cellH, f); continue; }
            // A white mask from resources, tinted with the same slot the
            // geometry used. Placed on whole pixels at its native size —
            // bilinear sampling then lands exactly on texel centres, so the
            // one-pixel strokes stay one pixel.
            const int s = icons->px;
            const int x = (int)std::lround(ox[b] * cellW);
            const int y = (bandPx(cellH) - s) / 2;
            cb.sprite((float)x / cellW, (float)y / cellH, (float)s / cellW, (float)s / cellH,
                      (uint16_t)icons->id[f ? ICON_FILLED : b], 1.0f, f ? c : shown);
        }
    }

    const int tlen = Utf8Width(title);
    cb.text((cols - tlen) / 2, CAPTION_ROW, title,
            focused ? TEXT : TEXT_MUTED, BG_RAISED, ATTR_BOLD);
    // No rule under the band: the raised band ends against the
    // window background, and the menu bar's own rule below is the only line.
}

void ButtonSpans(int cols, int cellW, int cellH, ButtonSpan out[BTN_COUNT]) {
    float ox[BTN_COUNT];
    boxOrigins(cols, cellW, cellH, ox);
    const float bw = boxW(cellW, cellH);
    for (int b = 0; b < BTN_COUNT; ++b) {
        // Two pixels of slack on each side: an 11 px target is small, and the
        // slack costs nothing because the caption around it only drags.
        out[b].x0 = (int)(ox[b] * cellW) - 2;
        out[b].x1 = (int)((ox[b] + bw) * cellW) + 2;
    }
}

int HitTest(int px_, int py_, int cols, int cellW, int cellH) {
    // The BOX, not the whole band: the band is a third taller than a row and
    // the box is barely half of it, so going by the band lit a box up with
    // the pointer a third of a row above or below it — the taller the cell,
    // the further off it looked. Two pixels of slack, as sideways.
    const int box = CaptionBoxPx(cellH);
    const int top = (CaptionHeightPx(cellH) - box) / 2;
    if (py_ < top - 2 || py_ >= top + box + 2) return BTN_NONE;
    ButtonSpan spans[BTN_COUNT];
    ButtonSpans(cols, cellW, cellH, spans);
    for (int b = 0; b < BTN_COUNT; ++b)
        if (px_ >= spans[b].x0 && px_ < spans[b].x1) return b;
    return BTN_NONE;
}

int CaptionHeightPx(int cellH) {
    return bandPx(cellH);
}

int CaptionBoxPx(int cellH) {
    return std::max(1, (int)std::lround(kBoxH * (float)cellH));
}

float CaptionTextPadPx(int cellH) {
    return kPadTop * (float)cellH;
}

void DrawRule(CellBuffer& cb, int y, bool raised) {
    for (int x = 0; x < cb.width(); ++x) cb.put(x, y, Gly::H, BORDER_DIM, BG_WINDOW);
    if (raised) cb.raiseRow(y, 0, cb.width(), CellBuffer::RAISE_HALF);
}

} // namespace chrome
