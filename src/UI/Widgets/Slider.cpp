#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>

// Slider (§8.4): a line and a square knob.
//
//   ╷────────■──────╷    continuous
//   ╷──╵──╵──■──╵──╵╷    discrete: a tick at every step
//   ╷───■═════■─────╷    range: the span between the knobs in `accent`
//
// All of it is geometry (§2.1), not glyphs: the line, the end and step ticks
// and the knob sit at the value's exact fraction of the track, so the ticks
// stay evenly spaced at any width and the knob lands exactly on its tick —
// in whole cells they could not, and drifted apart as the section resized.
// The value is not quantised either: a drag maps the pointer straight to it.

namespace ui {

namespace {

float snap(float v, float lo, float hi, float step) {
    if (!std::isfinite(v)) v = lo;
    v = std::clamp(v, std::min(lo, hi), std::max(lo, hi));
    if (step > 0.0f) {
        v = lo + std::round((v - lo) / step) * step;
        v = std::clamp(v, std::min(lo, hi), std::max(lo, hi));
    }
    return v;
}

/**
 * Where `v` sits, in cells: the knob's centre, from the middle of the second
 * cell to the middle of the last but one. Ticks, knobs and the pointer all
 * use this one mapping, so they always agree.
 */
float posOf(float v, float lo, float hi, const Rect& r) {
    const float t = hi != lo ? std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    return r.x + 1.5f + t * (float)(r.w - 3);
}

/** The value under the pointer: posOf() backwards. */
float valueAt(float mx, const Rect& r, float lo, float hi) {
    const float t = (mx - (float)r.x - 1.5f) / (float)(r.w - 3);
    return lo + std::clamp(t, 0.0f, 1.0f) * (hi - lo);
}

/** Keyboard and wheel step: the step itself, or 1 % — 0.1 % with Shift. */
float fineStep(const Context& ctx, float lo, float hi, float step) {
    if (step > 0.0f) return step;
    return (hi - lo) * (ctx.input().key_shift ? 0.001f : 0.01f);
}

uint8_t edgeSlot(Context& ctx, Id id) {
    if (ctx.disabled()) return TEXT_MUTED;
    if (ctx.isFocused(id)) return BORDER_FOCUS;
    if (ctx.isHot(id)) { ctx.setCursor(Cursor::Pointer); return ACCENT; }
    return BORDER;
}

/** Pixel metrics of a slider row: the stroke, and where the line runs. */
struct Metrics {
    float px, py;      // one pixel, in cells
    int   stroke;      // the line's and the ticks' thickness, px
    int   midY;        // the line's centre row, px from the top of the cell
};

Metrics metrics(const Context& ctx) {
    const Input& in = ctx.input();
    Metrics m;
    m.px = 1.0f / std::max(1, in.cell_w);
    m.py = 1.0f / std::max(1, in.cell_h);
    const BoxLine L = BoxLineIn(in.cell_w, in.cell_h);   // the frame lines' own stroke
    m.stroke = L.stroke;
    m.midY = L.y0 + L.stroke / 2;
    return m;
}

/** A vertical tick `hPx` tall, centred on the line at `x` (cells). */
void tick(CellBuffer& cb, const Metrics& m, float x, int row, int hPx, uint8_t slot) {
    const float left = std::round((x - 0.5f * m.stroke * m.px) / m.px) * m.px;   // whole pixels
    cb.shape(left, row + (m.midY - hPx / 2) * m.py, m.stroke * m.px, hPx * m.py, slot);
}

/** The line, the span, the ends and the step ticks; knobs are drawn by the caller. */
void drawTrack(Context& ctx, Id id, const Rect& r, float lo, float hi, float step,
               float spanFrom, float spanTo) {
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    const bool dis = ctx.disabled();
    const uint8_t edge = edgeSlot(ctx, id);
    const uint8_t line = dis ? (uint8_t)TEXT_MUTED : (uint8_t)BORDER;
    const Metrics m = metrics(ctx);

    // The line, end tick to end tick, as thick as the font's ─; the span
    // (range, or the part left of the knob) over it in `accent`.
    const float x0 = r.x + 0.5f, x1 = r.x + r.w - 0.5f;
    const float ly = r.y + (m.midY - m.stroke / 2) * m.py, lh = m.stroke * m.py;
    cb.shape(x0, ly, x1 - x0, lh, line);
    if (!dis && spanTo > spanFrom) cb.shape(spanFrom, ly, spanTo - spanFrom, lh, ACCENT);

    // The ends: 60 % of the row. The font's ├ ┤ run the full cell height and
    // past it, so sliders on neighbouring rows fused into one vertical line.
    const int endH = std::max(3, (int)std::lround(in.cell_h * 0.60f));
    const bool diamond = ctx.isFocused(id) && ctx.blinkOn();
    if (!diamond) tick(cb, m, x0, r.y, endH, edge);   // the ◆ takes that end
    tick(cb, m, x1, r.y, endH, edge);

    // Step ticks, shorter, on the exact position of each step — only when
    // they are at least two cells apart; denser they say nothing.
    if (step > 0.0f && hi != lo) {
        const int n = (int)std::lround(std::fabs(hi - lo) / step);
        if (n > 0 && (float)(r.w - 3) / n >= 2.0f) {
            const int stepH = std::max(3, (int)std::lround(in.cell_h * 0.36f));
            for (int i = 1; i < n; ++i) {
                const float x = posOf(lo + (hi - lo) * i / n, lo, hi, r);
                const bool span = x >= spanFrom && x <= spanTo;
                tick(cb, m, x, r.y, stepH, span && !dis ? (uint8_t)ACCENT : line);
            }
        }
    }
    if (diamond) cb.put(r.x, r.y, Gly::DIAMOND, BORDER_FOCUS, BG_PANEL);
}

/** The knob: a square the size of the font's ■, centred on `x`. */
void drawKnob(Context& ctx, const Rect& r, float x, uint8_t slot) {
    const Metrics m = metrics(ctx);
    const int side = std::max(5, std::min(ctx.input().cell_w, (int)std::lround(ctx.input().cell_h * 0.5f)));
    const float left = std::round((x - 0.5f * side * m.px) / m.px) * m.px;
    ctx.cells().shape(left, r.y + (m.midY - side / 2) * m.py, side * m.px, side * m.py, slot, 0.12f);
}

} // namespace

bool Slider(Context& ctx, const char* name, int w, float& v, float lo, float hi, float step) {
    const Rect r = ctx.allocate(w, 1);
    if (w < 4) return false;
    const Id id = ctx.makeId(name);
    ctx.focusable(id);
    ctx.interact(id, r);

    const float old = v;
    float nv = v;
    const Input& in = ctx.input();
    if (ctx.isActive(id) && in.mouse_down) nv = valueAt(in.mouse_x, r, lo, hi);
    if (ctx.isFocused(id) && !ctx.disabled()) {
        const float s = fineStep(ctx, lo, hi, step);
        if (in.key_left)  nv -= s;
        if (in.key_right) nv += s;
        if (in.key_home)  nv = lo;
        if (in.key_end)   nv = hi;
    }
    // interact() has just run, so "hovered" is this frame's answer — pointer
    // over the slider and inside the clip.
    if (ctx.isHovered(id)) {
        const float wh = ctx.takeWheel();
        if (wh != 0.0f) nv += wh * fineStep(ctx, lo, hi, step);
    }
    v = snap(nv, lo, hi, step);

    const float k = posOf(v, lo, hi, r);
    drawTrack(ctx, id, r, lo, hi, step, r.x + 0.5f, k);
    const bool dis = ctx.disabled();
    drawKnob(ctx, r, k, dis ? (uint8_t)TEXT_MUTED : ctx.isActive(id) ? (uint8_t)TEXT : (uint8_t)ACCENT);
    return v != old;
}

bool SliderRange(Context& ctx, const char* name, int w, float& a, float& b,
                 float lo, float hi, float step) {
    const Rect r = ctx.allocate(w, 1);
    if (w < 5) return false;
    const Id id = ctx.makeId(name);
    ctx.focusable(id);
    ctx.interact(id, r);

    // Which knob the keyboard and the drag move: remembered between frames,
    // because the press that picks it and the moves that use it are
    // different frames.
    float& which = ctx.state(id, 0.0f);

    const float oa = a, ob = b;
    if (a > b) std::swap(a, b);
    const Input& in = ctx.input();

    if (ctx.justPressed(id)) {
        // The nearer knob; on a tie (both in one place) the one the pointer
        // is past, so two knobs pushed together can always be pulled apart.
        const float ca = posOf(a, lo, hi, r), cb_ = posOf(b, lo, hi, r);
        const float da = std::fabs(in.mouse_x - ca), db = std::fabs(in.mouse_x - cb_);
        which = (da < db || (da == db && in.mouse_x < ca)) ? 0.0f : 1.0f;
    }
    if (ctx.isActive(id) && in.mouse_down) {
        const float val = valueAt(in.mouse_x, r, lo, hi);
        if (which < 0.5f) a = std::min(val, b);
        else              b = std::max(val, a);
    }
    if (ctx.isFocused(id) && !ctx.disabled()) {
        const float s = fineStep(ctx, lo, hi, step);
        if (in.key_up || in.key_down) which = which < 0.5f ? 1.0f : 0.0f;   // swap knobs
        float& t = which < 0.5f ? a : b;
        if (in.key_left)  t -= s;
        if (in.key_right) t += s;
        if (which < 0.5f) a = std::min(a, b);
        else              b = std::max(b, a);
    }
    a = snap(a, lo, hi, step);
    b = snap(b, lo, hi, step);
    if (a > b) a = b;

    const float ka = posOf(a, lo, hi, r), kb = posOf(b, lo, hi, r);
    drawTrack(ctx, id, r, lo, hi, step, ka, kb);
    const bool dis = ctx.disabled();
    const bool foc = ctx.isFocused(id) || ctx.isActive(id);
    // The knob the keys would move is drawn bright; the other stays accent.
    drawKnob(ctx, r, ka, dis ? (uint8_t)TEXT_MUTED : foc && which < 0.5f ? (uint8_t)TEXT : (uint8_t)ACCENT);
    drawKnob(ctx, r, kb, dis ? (uint8_t)TEXT_MUTED : foc && which >= 0.5f ? (uint8_t)TEXT : (uint8_t)ACCENT);
    return a != oa || b != ob;
}

} // namespace ui
