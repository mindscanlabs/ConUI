#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>

// The sidebar (§8.18): section icons in a column on the left edge.
//
//   ▓▓▓▓ ┌─[ PANEL ]──
//   ▓■▓▓ │                ■ — the active section, sidebar.active
//   ▓▓▓▓ │
//   ▓▣▓▓ │                ▣ — the others, sidebar.icon; under the pointer sidebar.hover
//   ▓▓▓▓ │
//
// A two-row slot per section, the icon one row tall and centred in it. The
// icons are raster masks from resources.dat, tinted with a theme slot — the
// same scheme as the caption boxes — and drawn at their own pixel size on
// whole pixels so their one-pixel strokes stay crisp.

namespace ui {

bool Sidebar(Context& ctx, const char* name, const Rect& r,
             const SidebarItem* items, int n, int& active, int iconPx,
             float bleedTop, float bleedBottom) {
    if (r.w < 2 || r.h < 2 || n <= 0 || !items) return false;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    const int cw = std::max(1, in.cell_w), chh = std::max(1, in.cell_h);
    active = std::clamp(active, 0, n - 1);
    const int old = active;

    ctx.pushId(name);
    const Id strip = ctx.makeId("strip");
    ctx.focusable(strip);
    const bool dis = ctx.disabled();
    const bool focused = ctx.isFocused(strip) && !dis;

    // Its own ground, no edge line — the colour alone sets it apart. A shape
    // rather than a surface, because it may reach past `r` into the rows
    // above and below — up to the middle of a ─ rule there — so the strip
    // meets the rules instead of stopping half a row short. On whole pixels.
    const int topPx = r.y * chh - (int)std::lround(std::max(0.0f, bleedTop) * chh);
    const int botPx = (r.y + r.h) * chh + (int)std::lround(std::max(0.0f, bleedBottom) * chh);
    const float y0 = (float)topPx / chh, gh = (float)(botPx - topPx) / chh;
    cb.shape((float)r.x, y0, (float)r.w, gh, SIDEBAR_BG);

    const int groundPx = r.w * cw;
    for (int i = 0; i < n; ++i) {
        const int sy = r.y + i * 2;
        if (sy + 2 > r.y + r.h) break;                  // no room: dropped whole
        const Rect hit{ r.x, sy, r.w, 2 };
        const Id iid = ctx.makeId(i);
        // The pointer answers to the ICON, not to the two-row slot around it:
        // the slot is four times its area, and a highlight that came
        // on half a row early read as a misaligned one — the more so the
        // bigger the cell. Two pixels of slack, as in the caption (§8.1).
        const bool hasIcon = items[i].icon >= 0 && iconPx > 0;
        const float hx = hasIcon ? r.x + (float)((r.w * cw - iconPx) / 2 - 2) / cw
                                 : (float)(r.x + (r.w - 1) / 2);
        const float hy = hasIcon ? sy + (float)((2 * chh - iconPx) / 2 - 2) / chh : (float)sy;
        const float hw = hasIcon ? (float)(iconPx + 4) / cw : 1.0f;
        const float hh = hasIcon ? (float)(iconPx + 4) / chh : 1.0f;
        if (ctx.interact(iid, hit, hx, hy, hw, hh)) {
            active = i;
            ctx.setFocus(strip);                        // then ↑/↓ carry on from here
        }
        const bool hot = ctx.isHot(iid) && !dis;
        if (hot) ctx.setCursor(Cursor::Pointer);
        Tooltip(ctx, items[i].tip);                     // attaches to iid, the last interact

        // The three states have their own theme slots (editable in the theme
        // editor); keyboard focus keeps the system-wide border.focus.
        const uint8_t tint = dis ? (uint8_t)TEXT_MUTED
                           : i == active ? (focused ? (uint8_t)BORDER_FOCUS : (uint8_t)SIDEBAR_ACTIVE)
                           : hot ? (uint8_t)SIDEBAR_HOVER : (uint8_t)SIDEBAR_ICON;
        if (items[i].icon >= 0 && iconPx > 0) {
            const int x = (groundPx - iconPx) / 2;                // px, from r.x
            const int y = (2 * chh - iconPx) / 2;                 // px, from sy
            cb.sprite(r.x + (float)x / cw, sy + (float)y / chh,
                      (float)iconPx / cw, (float)iconPx / chh,
                      (uint16_t)items[i].icon, 1.0f, tint);
        } else {
            // No icon in resources: a plain square still marks the slot.
            cb.put(r.x + (r.w - 1) / 2, sy, Gly::SQ_FILLED, tint, SIDEBAR_BG);
        }
    }

    if (focused) {
        if (in.key_up)   active = std::max(0, active - 1);
        if (in.key_down) active = std::min(n - 1, active + 1);
        if (in.key_home) active = 0;
        if (in.key_end)  active = n - 1;
    }

    ctx.popId();
    return active != old;
}

} // namespace ui
