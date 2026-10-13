#include "Layout.hpp"
#include "Theme.hpp"
#include "../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui {

// ---- Layout: the fractions that outlive the frame -------------------------

Layout::Node* Layout::find(const char* name) {
    for (Node& n : nodes_) if (n.name == name) return &n;
    return nullptr;
}

const Layout::Node* Layout::find(const char* name) const {
    for (const Node& n : nodes_) if (n.name == name) return &n;
    return nullptr;
}

float& Layout::frac(const char* name, float def) {
    if (Node* n = find(name)) {
        // A node loaded from data.bin learns its default on first use.
        if (!n->has_def) { n->def = def; n->has_def = true; }
        return n->value;
    }
    nodes_.push_back(Node{ name, def, def, true });
    return nodes_.back().value;
}

float Layout::defaultOf(const char* name) const {
    const Node* n = find(name);
    return n ? n->def : 0.5f;
}

std::string Layout::serialize() const {
    std::string out;
    char buf[32];
    for (const Node& n : nodes_) {
        if (!out.empty()) out += ';';
        std::snprintf(buf, sizeof buf, "=%.4f", n.value);
        out += n.name;
        out += buf;
    }
    return out;
}

void Layout::deserialize(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        size_t end = s.find(';', i);
        if (end == std::string::npos) end = s.size();
        const std::string item = s.substr(i, end - i);
        i = end + 1;
        const size_t eq = item.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        const std::string name = item.substr(0, eq);
        const char* num = item.c_str() + eq + 1;
        char* stop = nullptr;
        const float v = std::strtof(num, &stop);
        // A fraction outside (0, 1) or trailing garbage is a corrupt entry;
        // skip it and let the node come up at its default.
        if (!stop || *stop || !std::isfinite(v) || v <= 0.0f || v >= 1.0f) continue;
        if (Node* n = find(name.c_str())) n->value = v;
        else nodes_.push_back(Node{ name, v, 0.5f, false });
    }
}

// ---- Split: one node of the tree ------------------------------------------

namespace {

/** The first side's size in cells for a fraction, honouring the minimums. */
int sideFor(float f, int avail, int minA, int minB) {
    int s = (int)std::lround(f * avail);
    // Not enough room for both minimums: share proportionally instead of
    // letting one side go negative.
    if (minA + minB > avail) return std::clamp(s, 0, avail);
    return std::clamp(s, minA, avail - minB);
}

// Under the pointer or while dragged the seam is a 2 px line in `accent` —
// exactly 2 px at any density, and on whole pixels so it stays crisp. The
// wider pill with grip notches it replaced read as too heavy (§8.17).
constexpr int kHotPx = 2;

void drawSeam(CellBuffer& cb, Axis axis, const Rect& seam, bool hot, int cellW, int cellH) {
    if (axis == Axis::Cols) {
        const float x = (float)seam.x, y = (float)seam.y, h = (float)seam.h;
        if (hot) {
            const int off = std::max(0, (cellW - kHotPx) / 2);
            cb.shape(x + (float)off / cellW, y + 0.15f, (float)kHotPx / cellW, h - 0.30f, ACCENT);
        } else if (h > 1.0f) {
            cb.shape(x + 0.44f, y + 0.5f, 0.12f, h - 1.0f, BORDER_DIM, 0.5f, 1.0f);
        }
    } else {
        // The seam row is empty window — no panel ground carried into it.
        // The line runs along the TOP of the row, half a row under the
        // upper panel's frame rather than a whole row: it belongs to the
        // panel above. A hairline as thick as the vertical seam's is wide,
        // 2 px under the pointer; whole pixels. The grip is still the row.
        const float x = (float)seam.x, y = (float)seam.y, w = (float)seam.w;
        const int t = hot ? kHotPx : std::max(1, (int)std::lround(cellW * 0.12f));
        const float top = y;
        if (hot) cb.shape(x + 0.5f, top, w - 1.0f, (float)t / cellH, ACCENT);
        else if (w > 2.0f) cb.shape(x + 1.0f, top, w - 2.0f, (float)t / cellH, BORDER_DIM, 0.5f, 1.0f);
    }
}

} // namespace

bool Split(Context& ctx, Layout& lay, const char* name, Axis axis, const Rect& r,
           float def, int minA, int minB, Rect& a, Rect& b) {
    const bool cols = axis == Axis::Cols;
    const int avail = (cols ? r.w : r.h) - 1;          // minus the seam
    if (avail < 2) { a = r; b = Rect{ r.x, r.y, 0, 0 }; return false; }

    float& f = lay.frac(name, def);
    const Id id = ctx.makeId(name);
    int s = sideFor(f, avail, minA, minB);
    auto grip = [&](int side) {                        // the seam cell row or column
        return cols ? Rect{ r.x + side, r.y, 1, r.h } : Rect{ r.x, r.y + side, r.w, 1 };
    };
    Rect seam = grip(s);

    ctx.interact(id, seam);
    const Input& in = ctx.input();
    const float mouse = cols ? in.mouse_x : in.mouse_y;
    const int start = cols ? r.x : r.y;

    // Where inside the seam cell the drag took hold, so the seam does not
    // jump by half a cell on the first move. NaN = no drag this press.
    float& grab = ctx.state(id, NAN);
    bool changed = false;

    if (ctx.justPressed(id)) {
        if (in.mouse_double) {
            f = lay.defaultOf(name);
            grab = NAN;                   // the reset must not become a drag
            changed = true;
        } else {
            grab = mouse - (float)(start + s);
        }
    } else if (ctx.isActive(id) && in.mouse_down && std::isfinite(grab)) {
        // Pixel step: the pointer's FRACTIONAL cell position goes into the
        // fraction unrounded. Only the seam's position is rounded, below.
        float nf = (mouse - grab - (float)start) / (float)avail;
        if (minA + minB <= avail)
            nf = std::clamp(nf, (float)minA / avail, (float)(avail - minB) / avail);
        nf = std::clamp(nf, 0.0f, 1.0f);
        if (nf != f) { f = nf; changed = true; }
    }

    s = sideFor(f, avail, minA, minB);
    seam = grip(s);
    if (cols) {
        a = Rect{ r.x, r.y, s, r.h };
        b = Rect{ r.x + s + 1, r.y, r.w - s - 1, r.h };
    } else {
        a = Rect{ r.x, r.y, r.w, s };
        b = Rect{ r.x, r.y + s + 1, r.w, r.h - s - 1 };
        // The lower side's top frame is drawn raised towards the seam's line,
        // halving the space between them; only the drawing moves.
        ctx.cells().raiseRow(b.y, b.x, b.w);
    }

    const bool hot = ctx.isHot(id) || ctx.isActive(id);
    if (hot) ctx.setCursor(cols ? Cursor::ResizeH : Cursor::ResizeV);
    drawSeam(ctx.cells(), axis, seam, hot, std::max(1, in.cell_w), std::max(1, in.cell_h));
    return changed || ctx.isActive(id);
}

} // namespace ui
