#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>

// Progress (§8.3). The smooth bar is a pair of rectangles in the shape layer:
// the fill's width is the exact fraction, so it advances by fractions of a
// pixel instead of stepping by eighths of a cell.

namespace ui {

namespace {

/** The dithered track both smooth bars run in; returns its top edge. */
float track(CellBuffer& cb, const Rect& r) {
    const float y0 = r.y + (1.0f - BAR_H) * 0.5f;
    cb.shape((float)r.x, y0, (float)r.w, BAR_H, DITHER, BAR_R);
    return y0;
}

} // namespace

void ProgressBar(Context& ctx, int w, float v, uint8_t slot) {
    const Rect r = ctx.allocate(w, 1);
    if (w <= 0) return;
    CellBuffer& cb = ctx.cells();
    const float y0 = track(cb, r);
    const float f = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    if (f > 0.0f)
        cb.shape((float)r.x, y0, w * f, BAR_H, ctx.disabled() ? (uint8_t)TEXT_MUTED : slot, BAR_R);
}

void ProgressRetro(Context& ctx, int w, float v, uint8_t slot) {
    const Rect r = ctx.allocate(w, 1);
    if (w < 3) return;
    CellBuffer& cb = ctx.cells();
    const int run = w - 2;
    const float f = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    // Rounded DOWN: a retro bar that shows full at 97 % is lying.
    const int n = (int)std::floor(f * run + 1e-4f);
    cb.put(r.x, r.y, '[', TEXT_MUTED, BG_PANEL);
    // Filled cells ■ in `slot` (retro.filled by default); the rest of the
    // scale □ in retro.empty, so the whole length stays visible. Both are
    // theme slots, editable in the theme editor.
    for (int i = 0; i < run; ++i) {
        const bool on = i < n;
        const uint8_t ink = !on ? (uint8_t)RETRO_OFF : ctx.disabled() ? (uint8_t)TEXT_MUTED : slot;
        cb.put(r.x + 1 + i, r.y, on ? Gly::SQ_FILLED : Gly::SQ_HOLLOW, ink, BG_PANEL);
    }
    cb.put(r.x + w - 1, r.y, ']', TEXT_MUTED, BG_PANEL);
}

void ProgressBusy(Context& ctx, int w, uint8_t slot) {
    const Rect r = ctx.allocate(w, 1);
    if (w <= 0) return;
    CellBuffer& cb = ctx.cells();
    const float y0 = track(cb, r);
    if (ctx.disabled()) return;

    ctx.animate(1.0 / 30.0);        // it moves by itself; 30 Hz is smooth enough
    // One of the system's two animations (§9): a highlight that swings along
    // the track and back, easing into each turn, built from overlapping
    // translucent rectangles — still one flat colour, no gradient. The soft
    // tail is always BEHIND it: on the left going right, on the right coming
    // back.
    const float p = (float)std::fmod(ctx.input().time / 1.6, 2.0);   // 1.6 s per pass
    const bool forward = p < 1.0f;
    float ph = forward ? p : 2.0f - p;
    ph = ph * ph * (3.0f - 2.0f * ph);                               // ease in and out
    const float bw = w * 0.28f;
    const float bx = r.x + (w - bw) * ph;
    for (int i = 0; i < 6; ++i) {
        const float t = i / 6.0f;
        const float tail = bw * t * 0.5f, lead = bw * t * 0.15f;
        const float sx = std::max((float)r.x, bx + (forward ? tail : lead));
        const float ex = std::min((float)(r.x + w), bx + bw - (forward ? lead : tail));
        if (ex > sx) cb.shape(sx, y0, ex - sx, BAR_H, slot, BAR_R, 0.20f);
    }
}

} // namespace ui
