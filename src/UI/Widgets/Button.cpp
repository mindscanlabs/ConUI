#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

namespace ui {

bool Button(Context& ctx, str::Id label, int flags) {
    return Button(ctx, str::T(label), flags);
}

bool Button(Context& ctx, const char* label, int flags) {
    CellBuffer& cb = ctx.cells();
    const int lw = Utf8Width(label);
    const Rect r = ctx.allocate(lw + 4, 1);       // "[ LABEL ]"
    const Id id = ctx.makeId(label);

    ctx.focusable(id);
    if (flags & BTN_DEFAULT) ctx.preferFocus(id);    // where a modal puts the focus
    const bool clicked = ctx.interact(id, r);

    const bool dis = ctx.disabled();
    const bool down = ctx.isPressed(id);           // slid off = not pressed
    const bool hot = ctx.isHot(id) && (!ctx.isActive(id) || down);
    const bool focused = ctx.isFocused(id);
    const bool danger = (flags & BTN_DANGER) != 0;
    const uint8_t accent = danger ? (uint8_t)DANGER : (uint8_t)ACCENT;
    const int xr = r.x + r.w - 1;

    if (ctx.isHot(id) && !dis) ctx.setCursor(Cursor::Pointer);

    // The states from §8.2, and nothing invented: brackets by default,
    // inverted under the pointer, filled while held.
    if (dis) {
        cb.put(r.x, r.y, '[', TEXT_MUTED, BG_PANEL);
        cb.text(r.x + 2, r.y, label, TEXT_MUTED, BG_PANEL);
        cb.put(xr, r.y, ']', TEXT_MUTED, BG_PANEL);
        return false;
    }
    if (down || hot) {
        const uint32_t l = down ? Gly::SHADE_75 : Gly::RIGHT_HALF;
        const uint32_t rr = down ? Gly::SHADE_75 : Gly::LEFT_HALF;
        cb.put(r.x, r.y, l, accent, BG_PANEL);
        cb.fill(r.x + 1, r.y, r.w - 2, 1, ' ', TEXT_INV, BG_SEL);
        cb.text(r.x + 2, r.y, label, TEXT_INV, BG_SEL, ATTR_BOLD);
        cb.put(xr, r.y, rr, accent, BG_PANEL);
    } else {
        const bool def = (flags & BTN_DEFAULT) != 0;
        const uint8_t edge = focused ? (uint8_t)BORDER_FOCUS : accent;
        cb.put(r.x, r.y, def ? Gly::DIAMOND : (uint32_t)'[', edge, BG_PANEL);
        cb.text(r.x + 2, r.y, label, danger ? (uint8_t)DANGER : (uint8_t)TEXT, BG_PANEL,
                def ? ATTR_BOLD : ATTR_NONE);
        cb.put(xr, r.y, def ? Gly::DIAMOND : (uint32_t)']', edge, BG_PANEL);
    }

    // Keyboard focus is a blinking marker on the left edge, not a colour
    // change — colour is already carrying hover, and two meanings on one
    // channel read as one.
    if (focused && !down && ctx.blinkOn())
        cb.put(r.x, r.y, Gly::DIAMOND, hot ? accent : (uint8_t)BORDER_FOCUS, BG_PANEL);

    return clicked;
}

} // namespace ui
