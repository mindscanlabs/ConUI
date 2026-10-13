#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>

// Tabs (§8.15), the classic TUI trick: the active tab is a title sunk into the frame,
// the inactive ones "break out" of it and hang their bottoms into the first
// row of the panel:
//
//   ┌─[ WIDGETS ]─┐ DATA ├─┐ INPUT ├──────────┐
//   │             └──────┘ └───────┘          │
//   │  content of the active tab              │

namespace ui {

Rect BeginTabs(Context& ctx, const char* name, const Rect& r,
               const char* const* labels, int n, int& active) {
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    active = std::clamp(active, 0, std::max(0, n - 1));
    ctx.pushId(name);
    const Id strip = ctx.makeId("strip");
    ctx.focusable(strip);
    const bool focused = ctx.isFocused(strip);
    const uint8_t edge = focused ? (uint8_t)BORDER_FOCUS : (uint8_t)BORDER;

    DrawFrame(cb, r, edge, BG_PANEL);

    // Lay the tabs out along the top border; those that do not fit are
    // dropped whole — half a tab reads as a different word.
    int cx = r.x + 1;
    for (int i = 0; i < n; ++i) {
        const int lw = Utf8Width(labels[i]);
        const int tw = lw + 4;
        if (cx + tw > r.x + r.w - 1) break;
        const bool on = i == active;
        const Rect hit{ cx, r.y, tw, on ? 1 : 2 };
        const Id tid = ctx.makeId(i);
        if (ctx.interact(tid, hit) && in.mouse_released) {
            active = i;
            ctx.setFocus(strip);        // then ←/→ carry on from the tab clicked
        }
        const bool hot = ctx.isHot(tid) && !on;
        if (ctx.isHot(tid)) ctx.setCursor(Cursor::Pointer);

        if (on) {
            // The active tab IS the panel title.
            DrawFrameTitle(cb, cx - 1, r.y, labels[i], edge, BG_PANEL, ACCENT);
            if (focused && ctx.blinkOn()) cb.put(cx, r.y, Gly::DIAMOND, BORDER_FOCUS, BG_PANEL);
        } else {
            // The label rises above the frame line: a cap of panel ground
            // under it, like a title's (§8.1, §8.15), from the tab's left
            // stroke (┐) to its right one (├) — not just under the letters.
            cb.surface(cx, r.y, lw + 4, 1, BG_PANEL, EDGE_CAP | EDGE_L | EDGE_R);
            cb.put(cx, r.y, Gly::TR, edge, BG_PANEL);
            cb.put(cx + 1, r.y, ' ', edge, BG_PANEL);
            cb.text(cx + 2, r.y, labels[i], hot ? (uint8_t)ACCENT : (uint8_t)TEXT2, BG_PANEL);
            cb.put(cx + 2 + lw, r.y, ' ', edge, BG_PANEL);
            cb.put(cx + 3 + lw, r.y, Gly::TEE_R, edge, BG_PANEL);
            // The tab's bottom hangs into the panel's first row, drawn a
            // quarter of a row up so it sits closer under the label.
            cb.raiseRow(r.y + 1, cx, tw, CellBuffer::RAISE_QUARTER);
            cb.put(cx, r.y + 1, Gly::BL, edge, BG_PANEL);
            for (int k = 1; k < tw - 1; ++k) cb.put(cx + k, r.y + 1, Gly::H, edge, BG_PANEL);
            cb.put(cx + tw - 1, r.y + 1, Gly::BR, edge, BG_PANEL);
        }
        cx += tw + 1;                                   // one ─ between tabs
    }

    // The strip itself takes ←/→ when focused; it is one stop in the Tab order.
    if (focused && n > 0) {
        if (in.key_left)  active = (active + n - 1) % n;
        if (in.key_right) active = (active + 1) % n;
    }

    ctx.pushId(active);                                 // each tab its own id scope
    const Rect inner{ r.x + 1, r.y + 2, r.w - 2, r.h - 3 };
    ctx.pushRegion(inner);
    return inner;
}

void EndTabs(Context& ctx) {
    ctx.popRegion();
    ctx.popId();
    ctx.popId();
}

} // namespace ui
