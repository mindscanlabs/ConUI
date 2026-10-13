#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Charts (§8.20):
//
//   ┌─[ THROUGHPUT ]──────────────────────── ■ read ■ write ─┐
//   │   120 ┤          ╭──╮                                   │
//   │    80 ┤ ┄┄┄┄┄┄┄╭╯┄┄┄╰╮┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄ │
//   │    40 ┤    ╭──╯      ╰───╮                                │
//   │     0 └────┬─────┬─────┬─────┬────────────────────────── │
//   │           MO    TU    WE    TH                           │
//   └─────────────────────────────────────────────────────────┘
//
// What reads as text — the frame, the axis lines, the labels, the legend — is
// glyphs on the grid. The data is geometry (§2.1): lines are segments, bars
// are rectangles, the grid is 1 px dashes, so a value lands on its own pixel
// rather than on the nearest cell. So are the ticks along a category or value
// axis that runs across: they stand where their value is, not in whole cells.
// The value ticks up the side sit on whole rows, so a label, its ┤ and its
// grid line always share one row.

namespace ui {

namespace {

constexpr int   kTickRows = 3;       // rows between value ticks, at least

/** The value axis: `m` intervals of `step`, starting at `lo`. */
struct Scale {
    float lo = 0.0f, step = 1.0f;
    int   m = 1;
    float at(int k) const { return lo + step * k; }
    float frac(float v) const { return (v - lo) / (step * m); }   // 0 at lo, 1 at the top tick
};

/** The smallest 1, 2, 2.5 or 5 × 10^k that is at least `x`. */
float niceCeil(float x) {
    if (!(x > 0.0f)) return 1.0f;
    const float p = std::pow(10.0f, std::floor(std::log10(x)));
    for (float k : { 1.0f, 2.0f, 2.5f, 5.0f })
        if (k * p >= x * 0.9999f) return k * p;
    return 10.0f * p;
}

/**
 * `m` intervals on nice steps covering dmin..dmax. Explicit bounds win when
 * given (vmin < vmax); bars pass `fromZero`, since a bar is measured from 0.
 */
Scale makeScale(float dmin, float dmax, int m, float vmin, float vmax, bool fromZero) {
    Scale s;
    s.m = std::max(1, m);
    if (vmin < vmax) { s.lo = vmin; s.step = (vmax - vmin) / s.m; return s; }
    if (fromZero) { dmin = std::min(dmin, 0.0f); dmax = std::max(dmax, 0.0f); }
    if (!(dmax > dmin)) dmax = dmin + 1.0f;
    s.step = niceCeil((dmax - dmin) / s.m);
    for (int guard = 0; guard < 8; ++guard) {
        s.lo = std::floor(dmin / s.step) * s.step;
        if (s.at(s.m) >= dmax - s.step * 1e-4f) break;
        s.step = niceCeil(s.step * 1.01f);     // the next nice step up
    }
    return s;
}

/** A tick label: as many decimals as the step needs, whole thousands as k. */
std::string tickLabel(float v, float step) {
    char b[32];
    if (step >= 1000.0f && std::fmod(step, 1000.0f) == 0.0f) {
        std::snprintf(b, sizeof b, "%gk", v / 1000.0f);
    } else {
        int dec = 0;
        for (float s = step; dec < 3 && std::fabs(s - std::round(s)) > 1e-3f; s *= 10.0f) ++dec;
        std::snprintf(b, sizeof b, "%.*f", dec, v);
    }
    return std::strcmp(b, "-0") == 0 ? std::string("0") : std::string(b);
}

/** Data range over every finite value; false when there is none. */
bool dataRange(const ChartSeries* series, int ns, int n, float& lo, float& hi) {
    lo = INFINITY; hi = -INFINITY;
    for (int j = 0; j < ns; ++j)
        for (int i = 0; i < n; ++i) {
            const float v = series[j].values ? series[j].values[i] : NAN;
            if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
        }
    if (lo > hi) { lo = 0.0f; hi = 1.0f; return false; }
    return true;
}

/** The frame, its title, and " ■ name ■ name " in the top border. Returns the inside. */
Rect chartFrame(CellBuffer& cb, const Rect& r, const char* title, const ChartSeries* series, int ns) {
    DrawFrame(cb, r, BORDER, BG_PANEL);
    const int used = DrawPanelTitle(cb, r, title, BORDER);
    // The legend sits where a table shows its row count, and is dropped, not
    // cut, when the frame is too narrow for it.
    int lw = 1;
    for (int i = 0; i < ns; ++i) lw += 3 + Utf8Width(series[i].name ? series[i].name : "");
    if (ns > 0 && used + lw + 4 <= r.w) {
        int x = r.x + r.w - 2 - lw;
        cb.put(x++, r.y, ' ', BORDER, BG_PANEL);
        for (int i = 0; i < ns; ++i) {
            cb.put(x++, r.y, Gly::SQ_FILLED, series[i].slot, BG_PANEL);
            cb.put(x++, r.y, ' ', BORDER, BG_PANEL);
            x += cb.text(x, r.y, series[i].name ? series[i].name : "", TEXT_MUTED, BG_PANEL);
            cb.put(x++, r.y, ' ', BORDER, BG_PANEL);
        }
    }
    // Right under the title, as in every section (§8.1), with a column of
    // margin inside each side.
    return Rect{ r.x + 2, r.y + 1, r.w - 4, r.h - 2 };
}

/** A 1 px dashed line on the pixel row through `y`, from x0 to x1 (cells). */
void dashH(CellBuffer& cb, const Input& in, float x0, float x1, float y) {
    const float px = 1.0f / in.cell_w, py = 1.0f / in.cell_h;
    const float yy = std::floor(y * in.cell_h) * py;
    for (float x = x0; x < x1; x += 6.0f * px) cb.shape(x, yy, std::min(3.0f * px, x1 - x), py, BORDER_DIM);
}

/**
 * A tick under the ─ of an axis at `x` (cells): a short stroke down from the
 * line, as thick as it, at the exact position — a ┬ glyph could only sit in
 * a whole cell, and the ticks came out uneven as the chart was resized. The
 * hover guide and dots use the same x, so they land on the tick.
 */
void tickDown(CellBuffer& cb, const Input& in, float x, int row) {
    const BoxLine L = BoxLineIn(in.cell_w, in.cell_h);   // the ─ it hangs from
    const float px = 1.0f / in.cell_w, py = 1.0f / in.cell_h;
    const int h = std::max(3, (int)std::lround(in.cell_h * 0.30f));
    cb.shape(std::round(x * in.cell_w - 0.5f * L.stroke) * px, row + L.y0 * py,
             L.stroke * px, (h + L.stroke / 2) * py, TEXT_MUTED);
}

/** A 1 px dashed line down the pixel column through `x`, from y0 to y1 (cells). */
void dashV(CellBuffer& cb, const Input& in, float x, float y0, float y1) {
    const float px = 1.0f / in.cell_w, py = 1.0f / in.cell_h;
    const float xx = std::floor(x * in.cell_w) * px;
    for (float y = y0; y < y1; y += 6.0f * py) cb.shape(xx, y, px, std::min(3.0f * py, y1 - y), BORDER_DIM);
}

/**
 * Tangents for a monotone cubic through equally spaced points (Fritsch–
 * Carlson): the curve is smooth, and it never swings past a data point —
 * a smoothed chart must not invent a peak the data does not have.
 */
std::vector<float> monotoneTangents(const std::vector<float>& y, float h) {
    const int n = (int)y.size();
    std::vector<float> t((size_t)n, 0.0f);
    if (n < 2) return t;
    std::vector<float> d((size_t)n - 1);
    for (int k = 0; k + 1 < n; ++k) d[(size_t)k] = (y[(size_t)k + 1] - y[(size_t)k]) / h;
    t[0] = d[0];
    t[(size_t)n - 1] = d[(size_t)n - 2];
    for (int i = 1; i + 1 < n; ++i)
        t[(size_t)i] = d[(size_t)i - 1] * d[(size_t)i] <= 0.0f ? 0.0f : (d[(size_t)i - 1] + d[(size_t)i]) * 0.5f;
    for (int k = 0; k + 1 < n; ++k) {
        if (d[(size_t)k] == 0.0f) { t[(size_t)k] = t[(size_t)k + 1] = 0.0f; continue; }
        const float a = t[(size_t)k] / d[(size_t)k], b = t[(size_t)k + 1] / d[(size_t)k];
        const float s = a * a + b * b;
        if (s > 9.0f) {
            const float tau = 3.0f / std::sqrt(s);
            t[(size_t)k] = tau * a * d[(size_t)k];
            t[(size_t)k + 1] = tau * b * d[(size_t)k];
        }
    }
    return t;
}

/** "LABEL\nname  value\n…" for the hovered category. */
std::string tooltipText(const ChartSeries* series, int ns, const char* const* labels, int i, float step) {
    std::string s = labels && labels[i] ? labels[i] : std::to_string(i + 1);
    int nw = 0;
    for (int j = 0; j < ns; ++j) nw = std::max(nw, Utf8Width(series[j].name ? series[j].name : ""));
    for (int j = 0; j < ns; ++j) {
        const char* name = series[j].name ? series[j].name : "";
        const float v = series[j].values ? series[j].values[i] : NAN;
        s += "\n";
        s += name;
        s.append((size_t)(nw - Utf8Width(name) + 2), ' ');
        s += std::isfinite(v) ? tickLabel(v, step / 10.0f) : std::string("—");
    }
    return s;
}

/** The label under a point or a bar, centred on `cx`, kept inside [xMin, xMax]. */
void centredLabel(CellBuffer& cb, const char* s, float cx, int y, int xMin, int xMax) {
    const int w = Utf8Width(s);
    const int x = std::clamp((int)std::floor(cx) - (w - 1) / 2, xMin, std::max(xMin, xMax - w + 1));
    cb.text(x, y, s, TEXT_MUTED, BG_PANEL);
}

/**
 * The vertical value axis shared by the line chart and the vertical bars:
 * labels right-aligned before column `ax`, ┤ on the tick rows, └ on the
 * baseline row and ─ along it to `x1`.
 */
void valueAxisV(CellBuffer& cb, const Scale& s, int ax, int x1, int axisRow, int sp) {
    for (int k = 0; k <= s.m; ++k) {
        const std::string l = tickLabel(s.at(k), s.step);
        cb.text(ax - 1 - Utf8Width(l.c_str()), axisRow - k * sp, l.c_str(), TEXT_MUTED, BG_PANEL);
    }
    for (int row = axisRow - s.m * sp; row < axisRow; ++row)
        cb.put(ax, row, (axisRow - row) % sp == 0 ? Gly::TEE_L : Gly::V, TEXT_MUTED, BG_PANEL);
    cb.put(ax, axisRow, Gly::BL, TEXT_MUTED, BG_PANEL);
    for (int x = ax + 1; x <= x1; ++x) cb.put(x, axisRow, Gly::H, TEXT_MUTED, BG_PANEL);
}

int widestTick(const Scale& s) {
    int w = 0;
    for (int k = 0; k <= s.m; ++k) w = std::max(w, Utf8Width(tickLabel(s.at(k), s.step).c_str()));
    return w;
}

} // namespace

// ---- line chart -------------------------------------------------------------------------

void LineChart(Context& ctx, const char* name, const char* title, const Rect& r,
               const ChartSeries* series, int ns, int n, const char* const* labels,
               float vmin, float vmax, int flags) {
    if (r.w < 16 || r.h < 8 || n <= 0 || ns <= 0) return;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    const Id id = ctx.makeId(name);
    const Rect a = chartFrame(cb, r, title, series, ns);

    // ---- the value axis: whole rows between ticks, the labels left of it ----
    const int axisRow = a.y + a.h - 2, labelRow = a.y + a.h - 1;
    const int m = std::max(1, (axisRow - a.y) / kTickRows);
    const int sp = std::max(1, (axisRow - a.y) / m);
    float dmin, dmax;
    dataRange(series, ns, n, dmin, dmax);
    const Scale s = makeScale(dmin, dmax, m, vmin, vmax, false);
    const int ax = a.x + widestTick(s) + 1;
    const int px0 = ax + 1, px1 = a.x + a.w - 1, top = axisRow - m * sp;
    if (px1 - px0 < 3) return;

    // Points run from the middle of the first plot column to the middle of
    // the last; values map onto the tick rows' centres.
    const float xL = px0 + 0.5f, xR = px1 + 0.5f;
    const float dx = n > 1 ? (xR - xL) / (n - 1) : 0.0f;
    auto X = [&](int i) { return n > 1 ? xL + dx * i : (xL + xR) * 0.5f; };
    auto Y = [&](float v) { return axisRow + 0.5f - s.frac(v) * (float)(m * sp); };
    const float px = 1.0f / in.cell_w, py = 1.0f / in.cell_h;

    if (flags & CHART_GRID)
        for (int k = 1; k <= m; ++k) dashH(cb, in, (float)px0, (float)(px1 + 1), axisRow - k * sp + 0.5f);

    // ---- hover: the nearest point, a guide down to the axis -----------------
    const Rect plot{ px0, top, px1 - px0 + 1, axisRow - top + 1 };
    ctx.interact(id, plot);
    int hov = -1;
    if (ctx.isHot(id) && plot.contains(in.mouse_x, in.mouse_y))
        hov = n > 1 ? std::clamp((int)std::lround((in.mouse_x - xL) / dx), 0, n - 1) : 0;
    if (hov >= 0)
        cb.shape(std::round(X(hov) * in.cell_w - 0.5f) * px, top + 0.5f, px, (float)(axisRow - top), TEXT_MUTED, 0.0f, 0.7f);

    // ---- the lines -------------------------------------------------------------
    const float thick = std::max(1.5f, in.cell_h * 0.1f);
    cb.pushClip(px0, top, px1 - px0 + 1, axisRow - top + 1);
    for (int j = 0; j < ns; ++j) {
        const float* v = series[j].values;
        if (!v) continue;
        // A run of finite values is one line; a gap starts the next.
        for (int i0 = 0; i0 < n;) {
            if (!std::isfinite(v[i0])) { ++i0; continue; }
            int i1 = i0;
            while (i1 + 1 < n && std::isfinite(v[i1 + 1])) ++i1;
            if ((flags & CHART_SMOOTH) && i1 > i0) {
                std::vector<float> ys;
                for (int i = i0; i <= i1; ++i) ys.push_back(Y(v[i]));
                const std::vector<float> t = monotoneTangents(ys, dx);
                // Enough pieces that the curve reads as a curve: one per ~3 px.
                const int pieces = std::clamp((int)(dx * in.cell_w / 3.0f), 2, 48);
                for (int k = 0; k < i1 - i0; ++k) {
                    const float y0 = ys[(size_t)k], y1 = ys[(size_t)k + 1];
                    const float m0 = t[(size_t)k] * dx, m1 = t[(size_t)k + 1] * dx;
                    float lx = X(i0 + k), ly = y0;
                    for (int p = 1; p <= pieces; ++p) {
                        const float u = (float)p / pieces, u2 = u * u, u3 = u2 * u;
                        const float y = (2 * u3 - 3 * u2 + 1) * y0 + (u3 - 2 * u2 + u) * m0 +
                                        (-2 * u3 + 3 * u2) * y1 + (u3 - u2) * m1;
                        const float x = X(i0 + k) + dx * u;
                        cb.segment(lx, ly, x, y, series[j].slot, thick);
                        lx = x; ly = y;
                    }
                }
            } else {
                for (int i = i0; i < i1; ++i)
                    cb.segment(X(i), Y(v[i]), X(i + 1), Y(v[i + 1]), series[j].slot, thick);
                if (i1 == i0)   // a lone point is still data: a dot
                    cb.shape(X(i0) - 1.5f * thick * px, Y(v[i0]) - 1.5f * thick * py,
                             3 * thick * px, 3 * thick * py, series[j].slot, 0.5f);
            }
            i0 = i1 + 1;
        }
        // The hovered point: a dot on the line, the series' own colour.
        if (hov >= 0 && std::isfinite(v[hov])) {
            const float d = std::max(5.0f, thick * 3.5f);
            cb.shape(X(hov) - 0.5f * d * px, Y(v[hov]) - 0.5f * d * py, d * px, d * py,
                     series[j].slot, 0.5f);
        }
    }
    cb.popClip();

    // ---- the axes and their labels -------------------------------------------
    valueAxisV(cb, s, ax, px1, axisRow, sp);
    if (labels) {
        int lw = 1;
        for (int i = 0; i < n; ++i) lw = std::max(lw, Utf8Width(labels[i] ? labels[i] : ""));
        const int every = n > 1 ? std::max(1, (int)std::ceil((lw + 1) / std::max(dx, 0.01f))) : 1;
        for (int i = 0; i < n; i += every) {
            tickDown(cb, in, X(i), axisRow);
            if (labels[i]) centredLabel(cb, labels[i], X(i), labelRow, px0 - 1, a.x + a.w - 1);
        }
    }

    if (hov >= 0) Tooltip(ctx, tooltipText(series, ns, labels, hov, s.step).c_str());
}

// ---- bar chart --------------------------------------------------------------------------

void BarChart(Context& ctx, const char* name, const char* title, const Rect& r,
              const ChartSeries* series, int ns, int n, const char* const* labels,
              float vmax, int flags) {
    if (r.w < 16 || r.h < 8 || n <= 0 || ns <= 0) return;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    const Id id = ctx.makeId(name);
    const Rect a = chartFrame(cb, r, title, series, ns);
    const int axisRow = a.y + a.h - 2, labelRow = a.y + a.h - 1;
    float dmin, dmax;
    dataRange(series, ns, n, dmin, dmax);
    const float px = 1.0f / in.cell_w, py = 1.0f / in.cell_h;
    // Each category is a group: one bar per series across 70 % of the
    // group's width, with a sliver of air between neighbouring bars.
    auto groupBars = [&](int j, float g0, float gs, float& b0, float& bs) {
        const float inner = gs * 0.7f, each = inner / ns;
        b0 = g0 + (gs - inner) * 0.5f + j * each + each * 0.08f;
        bs = each * 0.84f;
    };
    auto value = [&](int j, int i) { return series[j].values ? series[j].values[i] : NAN; };
    int hov = -1;

    if (!(flags & CHART_HORIZONTAL)) {
        // ---- vertical: categories along the bottom, values up the left ------
        const int m = std::max(1, (axisRow - a.y) / kTickRows);
        const int sp = std::max(1, (axisRow - a.y) / m);
        const Scale s = makeScale(dmin, dmax, m, 0.0f, vmax, true);
        const int ax = a.x + widestTick(s) + 1;
        const int px0 = ax + 1, px1 = a.x + a.w - 1, top = axisRow - m * sp;
        if (px1 - px0 < 2) return;
        auto Y = [&](float v) { return axisRow + 0.5f - s.frac(v) * (float)(m * sp); };
        const float gw = (float)(px1 - px0 + 1) / n;
        const float base = Y(std::clamp(0.0f, s.lo, s.at(m)));

        if (flags & CHART_GRID)
            for (int k = 1; k <= m; ++k) dashH(cb, in, (float)px0, (float)(px1 + 1), axisRow - k * sp + 0.5f);
        const Rect plot{ px0, top, px1 - px0 + 1, axisRow - top + 1 };
        ctx.interact(id, plot);
        if (ctx.isHot(id) && plot.contains(in.mouse_x, in.mouse_y))
            hov = std::clamp((int)std::floor((in.mouse_x - px0) / gw), 0, n - 1);
        if (hov >= 0)    // the hovered group, raised behind its bars
            cb.shape(px0 + hov * gw, top + 0.5f, gw, base - top - 0.5f, BG_RAISED, 0.0f, 0.8f);

        cb.pushClip(px0, top, px1 - px0 + 1, axisRow - top + 1);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < ns; ++j) {
                const float v = value(j, i);
                if (!std::isfinite(v)) continue;
                float b0, bs;
                groupBars(j, px0 + i * gw, gw, b0, bs);
                const float yv = Y(v), y0 = std::min(yv, base), h = std::fabs(base - yv);
                if (h > 0.5f * py) cb.shape(b0, y0, std::max(bs, px), h, series[j].slot, BAR_R);
            }
        cb.popClip();

        valueAxisV(cb, s, ax, px1, axisRow, sp);
        if (labels) {
            int lw = 1;
            for (int i = 0; i < n; ++i) lw = std::max(lw, Utf8Width(labels[i] ? labels[i] : ""));
            const int every = std::max(1, (int)std::ceil((lw + 1) / gw));
            for (int i = 0; i < n; i += every)
                if (labels[i]) centredLabel(cb, labels[i], px0 + (i + 0.5f) * gw, labelRow, px0, a.x + a.w - 1);
        }
        if (hov >= 0) Tooltip(ctx, tooltipText(series, ns, labels, hov, s.step).c_str());
        return;
    }

    // ---- horizontal: categories down the left, values along the bottom -------
    int lw = 0;
    if (labels)
        for (int i = 0; i < n; ++i) lw = std::max(lw, Utf8Width(labels[i] ? labels[i] : ""));
    lw = std::min(lw, a.w / 3);
    const int ax = a.x + lw + (lw > 0 ? 1 : 0);
    const int px0 = ax + 1, px1 = a.x + a.w - 1;
    const int W = px1 - px0 + 1;
    if (W < 6 || axisRow - a.y < 1) return;
    // As many value ticks as their labels leave room for, two cells apart.
    Scale s;
    for (int m = std::max(1, W / 6); m >= 1; --m) {
        s = makeScale(dmin, dmax, m, 0.0f, vmax, true);
        if ((float)W / m >= widestTick(s) + 2 || m == 1) break;
    }
    const float xB = ax + 0.5f, xE = px1 + 0.5f;
    auto X = [&](float v) { return xB + s.frac(v) * (xE - xB); };
    const float gh = (float)(axisRow - a.y) / n;
    const float base = X(std::clamp(0.0f, s.lo, s.at(s.m)));

    if (flags & CHART_GRID)
        for (int k = 1; k <= s.m; ++k) dashV(cb, in, X(s.at(k)), (float)a.y, axisRow + 0.5f);
    const Rect plot{ px0, a.y, W, axisRow - a.y };
    ctx.interact(id, plot);
    if (ctx.isHot(id) && plot.contains(in.mouse_x, in.mouse_y))
        hov = std::clamp((int)std::floor((in.mouse_y - a.y) / gh), 0, n - 1);
    if (hov >= 0)
        cb.shape(base, a.y + hov * gh, xE - base, gh, BG_RAISED, 0.0f, 0.8f);

    cb.pushClip(px0 - 1, a.y, W + 1, axisRow - a.y);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < ns; ++j) {
            const float v = value(j, i);
            if (!std::isfinite(v)) continue;
            float b0, bs;
            groupBars(j, a.y + i * gh, gh, b0, bs);
            const float xv = X(v), x0 = std::min(xv, base), w = std::fabs(xv - base);
            if (w > 0.5f * px) cb.shape(x0, b0, w, std::max(bs, py), series[j].slot, BAR_R);
        }
    cb.popClip();

    // The axes: │ down the left, └ at the corner, ─ with ┬ ticks along the bottom.
    for (int row = a.y; row < axisRow; ++row) cb.put(ax, row, Gly::V, TEXT_MUTED, BG_PANEL);
    cb.put(ax, axisRow, Gly::BL, TEXT_MUTED, BG_PANEL);
    for (int x = px0; x <= px1; ++x) cb.put(x, axisRow, Gly::H, TEXT_MUTED, BG_PANEL);
    for (int k = 0; k <= s.m; ++k) {
        const float x = X(s.at(k));
        if (k > 0) tickDown(cb, in, x, axisRow);
        centredLabel(cb, tickLabel(s.at(k), s.step).c_str(), x, labelRow, a.x, a.x + a.w - 1);
    }
    // Category labels on their band's middle row, right-aligned to the axis;
    // a band thinner than a row shares it, and the first label keeps it.
    if (labels && lw > 0) {
        int lastRow = -1;
        for (int i = 0; i < n; ++i) {
            const int row = (int)std::floor(a.y + (i + 0.5f) * gh);
            if (row == lastRow || !labels[i]) continue;
            std::vector<uint32_t> cps = Utf8Decode(labels[i]);
            if ((int)cps.size() > lw) { cps.resize((size_t)lw); cps.back() = 0x2026; }
            int x = ax - 1 - (int)cps.size();
            for (uint32_t cp : cps) cb.put(x++, row, cp, i == hov ? (uint8_t)TEXT : (uint8_t)TEXT_MUTED, BG_PANEL);
            lastRow = row;
        }
    }
    if (hov >= 0) Tooltip(ctx, tooltipText(series, ns, labels, hov, s.step).c_str());
}

} // namespace ui
