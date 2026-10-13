#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

namespace ui {

void DrawFrame(CellBuffer& cb, const Rect& r, uint8_t fg, uint8_t bg) {
    if (r.w < 2 || r.h < 2) return;
    const int x = r.x, y = r.y, w = r.w, h = r.h;
    // The fill is a SURFACE — the panel, the popup, is its own layer and the
    // cells on it inherit (§2) — plus the cells themselves, so the frame
    // glyphs have a background slot to carry. Framed on all four edges: the
    // ground stops at the frame line, never half a cell outside it (§8.1).
    // Cells inside with the same background leave it to the surface.
    cb.surface(x, y, w, h, bg, EDGE_ALL);
    cb.fill(x, y, w, h, ' ', fg, bg);
    for (int i = x + 1; i < x + w - 1; ++i) {
        cb.put(i, y,         Gly::H, fg, bg);
        cb.put(i, y + h - 1, Gly::H, fg, bg);
    }
    for (int j = y + 1; j < y + h - 1; ++j) {
        cb.put(x,         j, Gly::V, fg, bg);
        cb.put(x + w - 1, j, Gly::V, fg, bg);
    }
    cb.put(x,         y,         Gly::TL, fg, bg);
    cb.put(x + w - 1, y,         Gly::TR, fg, bg);
    cb.put(x,         y + h - 1, Gly::BL, fg, bg);
    cb.put(x + w - 1, y + h - 1, Gly::BR, fg, bg);
}

void DrawFrameTitle(CellBuffer& cb, int x, int y, const char* title,
                    uint8_t fg, uint8_t bg, uint8_t titleFg) {
    int cx = x + 1;
    // The panel's ground starts at the frame line, so the upper half of the
    // title would sit on the bare window; a cap of ground above the line
    // puts all of "[ TITLE ]" on it (§8.1). Only for a layer colour — a cell
    // background such as bg.raised paints its whole cell by itself.
    if (bg == BG_PANEL)                  // …inside the brackets' ink, not the whole cells
        cb.surface(cx, y, Utf8Width(title) + 4, 1, bg, EDGE_CAP | EDGE_INK_L | EDGE_INK_R);
    cb.put(cx++, y, '[', fg, bg);
    cb.put(cx++, y, ' ', fg, bg);
    cx += cb.text(cx, y, title, titleFg, bg, ATTR_BOLD);
    cb.put(cx++, y, ' ', fg, bg);
    cb.put(cx,   y, ']', fg, bg);
}

int DrawPanelTitle(CellBuffer& cb, const Rect& r, const char* title, uint8_t edge, uint8_t titleFg) {
    if (!title || !*title || Utf8Width(title) + 6 > r.w) return 0;
    DrawFrameTitle(cb, r.x, r.y, title, edge, BG_PANEL, titleFg);
    return Utf8Width(title) + 5;
}

Rect BeginPanel(Context& ctx, const char* id, const char* title, const Rect& r,
                const char* status, int flags) {
    CellBuffer& cb = ctx.cells();
    const uint8_t edge = (flags & PANEL_FOCUS) ? (uint8_t)BORDER_FOCUS : (uint8_t)BORDER;

    DrawFrame(cb, r, edge, BG_PANEL);

    // Title and status are both sunk into the top border and
    // are dropped, not truncated, when the panel is too narrow for them:
    // half a title reads as a different word.
    const int used = DrawPanelTitle(cb, r, title, edge);
    if (status && *status) {
        const int sw = Utf8Width(status) + 2;
        if (used + sw + 4 <= r.w) {
            cb.put(r.x + r.w - 3 - sw + 1, r.y, ' ', edge, BG_PANEL);
            cb.text(r.x + r.w - 3 - sw + 2, r.y, status, TEXT_MUTED, BG_PANEL);
            cb.put(r.x + r.w - 3, r.y, ' ', edge, BG_PANEL);
        }
    }

    ctx.pushId(id ? id : "panel");

    // Inside: a margin column on the left, the scrollbar column on the right
    // (§8.14); the content starts on the row right under the title (with
    // no blank row in between). A flush panel (a full-width list)
    // skips the left/right margin.
    const bool flush = (flags & PANEL_FLUSH) != 0;
    const Rect inner = flush ? Rect{ r.x + 1, r.y + 1, r.w - 2, r.h - 2 }
                             : Rect{ r.x + 2, r.y + 1, r.w - 4, r.h - 2 };
    ctx.pushRegion(inner);
    return inner;
}

void EndPanel(Context& ctx) {
    ctx.popRegion();
    ctx.popId();
}

} // namespace ui
