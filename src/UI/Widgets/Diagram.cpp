#include "Widgets.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// Diagrams (§8.21): pictures of structure rather than of values — a tree of
// boxes (an expression, cube by cube) and a layered network (inputs, hidden
// layers, output, a line per weight).
//
//   ┌─[ EXPRESSION ]──────────────────┐     ┌─[ NETWORK ]──────────────────────┐
//   │               (−)               │     │   x ●╲═══●───●╲                  │
//   │             ╱     ╲             │     │       ╳      ╳  ╲               │
//   │          (1)       (^)          │     │   y ●╱───●═══●───● P             │
//   │                  ╱     ╲        │     │                                  │
//   │              (5/6)     (n)      │     │ wheel: zoom · drag: move · …     │
//   └─────────────────────────────────┘     └──────────────────────────────────┘
//
// The labels are text, so every box sits on whole cells; the boxes, the nodes
// and every connection are geometry (§2.1), placed at their exact positions.
// Both share one view: the wheel zooms about the cursor, a drag moves the
// picture, a double click shows it whole again. Zoom spreads the picture
// apart and leaves the letters their size — text on a cell grid cannot grow
// by 25 %, and a picture of structure needs room more than bigger letters.

namespace ui {

namespace {

/** What the context keeps for a diagram: its view. */
struct DiagView {
    float zoom = 1.0f, panX = 0.0f, panY = 0.0f;
    float lastX = 0.0f, lastY = 0.0f;   // where the drag was last frame
};

constexpr float kZoomMax = 16.0f;
constexpr float kZoomStep = 1.25f;      // one wheel notch

/** A picture point v on the screen: c + (v − c)·zoom + pan, c being the area's centre. */
struct ViewMap {
    float cx, cy, zoom, panX, panY;
    float x(float v) const { return cx + (v - cx) * zoom + panX; }
    float y(float v) const { return cy + (v - cy) * zoom + panY; }
};

/**
 * The view of the picture in `area`: routes the pointer through interact()
 * (an overlay on top takes it), applies the wheel, the drag and the double
 * click, and returns the mapping for this frame.
 */
ViewMap handleView(Context& ctx, Id id, const Rect& area) {
    DiagView& v = ctx.blob<DiagView>(id);
    const Input& io = ctx.input();
    const float cx = area.x + area.w * 0.5f, cy = area.y + area.h * 0.5f;
    ctx.interact(id, area);
    if (ctx.isHovered(id)) {
        if (io.mouse_double) {
            v.zoom = 1.0f;
            v.panX = v.panY = 0.0f;
        } else if (const float w = ctx.takeWheel(); w != 0.0f) {
            // The point under the cursor stays under it.
            const float z0 = v.zoom;
            v.zoom = std::clamp(z0 * std::pow(kZoomStep, w), 1.0f, kZoomMax);
            const float wx = (io.mouse_x - cx - v.panX) / z0, wy = (io.mouse_y - cy - v.panY) / z0;
            v.panX = io.mouse_x - cx - wx * v.zoom;
            v.panY = io.mouse_y - cy - wy * v.zoom;
        }
    }
    if (ctx.justPressed(id)) { v.lastX = io.mouse_x; v.lastY = io.mouse_y; }
    else if (ctx.isActive(id) && io.mouse_down) {
        v.panX += io.mouse_x - v.lastX;
        v.panY += io.mouse_y - v.lastY;
        v.lastX = io.mouse_x;
        v.lastY = io.mouse_y;
        ctx.animate(0.02);
    }
    if (ctx.isHot(id) || ctx.isActive(id)) ctx.setCursor(Cursor::Move);
    return ViewMap{ cx, cy, v.zoom, v.panX, v.panY };
}

/** "wheel: zoom · drag: move · double-click: reset" on the area's last row. */
void drawHint(CellBuffer& cb, const Rect& r) {
    cb.pushClip(r.x, r.y, r.w, r.h);
    cb.text(r.x + 1, r.y + r.h - 1, str::T(str::ID_DIAG_HINT), TEXT_MUTED, BG_PANEL);
    cb.popClip();
}

} // namespace

void ResetDiagram(Context& ctx, const char* name) {
    DiagView& v = ctx.blob<DiagView>(ctx.makeId(name));
    v.zoom = 1.0f;
    v.panX = v.panY = 0.0f;
}

// ---- the tree of boxes ------------------------------------------------------------------

int TreeDiagram(Context& ctx, const char* name, const Rect& r, const DiagramNode* nodes, int n, int flags) {
    if (r.w < 4 || r.h < 2 || n <= 0) return -1;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    const Id id = ctx.makeId(name);
    const ViewMap view = handleView(ctx, id, r);

    // Children, depth and the layout: leaves left to right, one slot each, a
    // parent over the middle of its children. A node whose parent is not an
    // earlier node is a root; several roots stand side by side.
    std::vector<std::vector<int>> kids((size_t)n);
    std::vector<int> depth((size_t)n, 0), roots;
    for (int i = 0; i < n; ++i) {
        const int p = nodes[i].parent;
        if (p >= 0 && p < i) { kids[(size_t)p].push_back(i); depth[(size_t)i] = depth[(size_t)p] + 1; }
        else roots.push_back(i);
    }
    std::vector<float> slotX((size_t)n, 0.0f);
    int leaves = 0;
    auto place = [&](auto&& self, int i) -> float {
        const std::vector<int>& k = kids[(size_t)i];
        if (k.empty()) return slotX[(size_t)i] = (float)leaves++;
        float s = 0.0f;
        for (int c : k) s += self(self, c);
        return slotX[(size_t)i] = s / k.size();
    };
    for (int root : roots) place(place, root);
    const int depthMax = *std::max_element(depth.begin(), depth.end());

    // Rows from the top, at most 3 apart, the deepest one above the last row
    // (the hint's). Each box on whole cells — its text needs them.
    const float top = (float)r.y, bottom = (float)(r.y + r.h - 1);
    const float dy = depthMax > 0 ? std::min(3.0f, (bottom - top - 1) / depthMax) : 0.0f;
    const float left = (float)(r.x + 1), width = (float)(r.w - 2);
    struct Box { int tx, ty, w; };
    std::vector<Box> box((size_t)n);
    for (int i = 0; i < n; ++i) {
        const int w = Utf8Width(nodes[i].label.c_str());
        const float x = view.x(left + (slotX[(size_t)i] + 0.5f) / std::max(1, leaves) * width);
        const float y = view.y(top + depth[(size_t)i] * dy);
        box[(size_t)i] = { (int)std::lround(x - w * 0.5f), (int)std::lround(y), w };
    }
    // The box under the pointer: the last one drawn wins, as on screen.
    int hov = -1;
    if (ctx.isHot(id) && r.contains(in.mouse_x, in.mouse_y))
        for (int i = 0; i < n; ++i) {
            const Box& b = box[(size_t)i];
            if (!nodes[i].hidden && in.mouse_x >= b.tx - 0.5f && in.mouse_x < b.tx + b.w + 0.5f &&
                in.mouse_y >= b.ty && in.mouse_y < b.ty + 1) hov = i;
        }

    cb.pushClip(r.x, r.y, r.w, r.h);
    for (int i = 0; i < n; ++i) {                    // connections first, under the boxes
        if (nodes[i].hidden) continue;
        for (int k : kids[(size_t)i]) {
            if (nodes[k].hidden) continue;
            const Box &p = box[(size_t)i], &c = box[(size_t)k];
            cb.segment(p.tx + p.w * 0.5f, p.ty + 0.94f, c.tx + c.w * 0.5f, c.ty + 0.06f, BORDER, 1.5f);
        }
    }
    for (int i = 0; i < n; ++i) {
        if (nodes[i].hidden) continue;
        const Box& b = box[(size_t)i];
        const uint8_t slot = nodes[i].slot;
        cb.shape(b.tx - 0.5f, b.ty + 0.06f, b.w + 1.0f, 0.88f, BG_RAISED, 0.35f);
        cb.shape(b.tx - 0.5f, b.ty + 0.06f, b.w + 1.0f, 0.88f, slot, 0.35f, 1.0f, i == hov ? 2.5f : 1.5f);
        cb.text(b.tx, b.ty, nodes[i].label.c_str(), slot, BG_PANEL, ATTR_BOLD);
    }
    cb.popClip();
    if (flags & DIAG_HINT) drawHint(cb, r);
    return hov;
}

// ---- the layered network ----------------------------------------------------------------

void NetDiagram(Context& ctx, const char* name, const Rect& r, const NetLayer* layers, int nl,
                const float* const* weights, int flags) {
    if (r.w < 8 || r.h < 2 || nl <= 0) return;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    const ViewMap view = handleView(ctx, ctx.makeId(name), r);

    // Columns from the first layer's names to the last one's: room on each
    // side for the widest of them.
    auto widest = [](const NetLayer& L) {
        int w = 0;
        if (L.labels)
            for (int k = 0; k < L.n; ++k) w = std::max(w, Utf8Width(L.labels[k] ? L.labels[k] : ""));
        return w;
    };
    const float top = (float)r.y, bottom = (float)(r.y + r.h - 1);
    const float left = (float)(r.x + std::max(4, widest(layers[0]) + 3));
    const float right = (float)(r.x + r.w - std::max(4, widest(layers[nl - 1]) + 3));
    auto px = [&](int layer) {
        return view.x(nl == 1 ? (left + right) * 0.5f : left + (right - left) * layer / (float)(nl - 1));
    };
    auto py = [&](int layer, int k) {
        const int c = layers[layer].n;
        return view.y(c == 1 ? (top + bottom) * 0.5f : top + (bottom - top) * (k + 0.5f) / c);
    };

    cb.pushClip(r.x, r.y, r.w, r.h);
    // A line per weight: `chart.1` positive, `chart.2` negative, thicker and
    // more opaque the larger it is next to the layer's largest. The faintest
    // are left out: they would only fog the picture. Without weights, every
    // connection alike.
    for (int L = 0; L + 1 < nl; ++L) {
        const int from = layers[L].n, to = layers[L + 1].n;
        const float* W = weights ? weights[L] : nullptr;
        if (!W) {
            for (int j = 0; j < to; ++j)
                for (int i = 0; i < from; ++i)
                    cb.segment(px(L), py(L, i), px(L + 1), py(L + 1, j), BORDER_DIM, 1.0f, 0.6f);
            continue;
        }
        float mx = 0.0f;
        for (int k = 0; k < from * to; ++k) mx = std::max(mx, std::fabs(W[k]));
        if (!(mx > 0.0f)) continue;
        for (int j = 0; j < to; ++j)
            for (int i = 0; i < from; ++i) {
                const float wv = W[(size_t)j * from + i];
                const float a = std::fabs(wv) / mx;
                if (a < 0.08f) continue;
                cb.segment(px(L), py(L, i), px(L + 1), py(L + 1, j), wv > 0 ? CHART_1 : CHART_2,
                           1.0f + 1.5f * a, 0.15f + 0.85f * a);
            }
    }
    // Round nodes that grow with the zoom, but slower than the picture, so
    // the crowded middle layers come apart as one zooms in.
    const float r0 = 0.36f * std::sqrt(view.zoom);
    const float aspect = (float)in.cell_w / (float)std::max(1, in.cell_h);   // a circle, not a cell-shaped oval
    for (int L = 0; L < nl; ++L)
        for (int k = 0; k < layers[L].n; ++k)
            cb.shape(px(L) - r0, py(L, k) - r0 * aspect, 2 * r0, 2 * r0 * aspect, layers[L].slot, 0.5f);
    // Names: the first layer's to the left of its nodes, the last one's to the right.
    for (int k = 0; layers[0].labels && k < layers[0].n; ++k)
        if (layers[0].labels[k])
            cb.textRight((int)std::lround(px(0) - r0) - 2, (int)(py(0, k) - 0.5f), layers[0].labels[k],
                         layers[0].slot, BG_PANEL);
    if (nl > 1)
        for (int k = 0; layers[nl - 1].labels && k < layers[nl - 1].n; ++k)
            if (layers[nl - 1].labels[k])
                cb.text((int)std::lround(px(nl - 1) + r0) + 1, (int)(py(nl - 1, k) - 0.5f),
                        layers[nl - 1].labels[k], layers[nl - 1].slot, BG_PANEL);
    cb.popClip();
    if (flags & DIAG_HINT) drawHint(cb, r);
}

} // namespace ui
