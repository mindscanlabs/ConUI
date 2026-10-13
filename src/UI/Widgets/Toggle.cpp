#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>

// Checkbox, radio and switch (§8.5). They share everything but the glyphs:
// one row, the whole row is the hit target (label included — a 3-cell target
// is too small to be the only one), and the value flips on a completed click
// or on Enter/Space.

namespace ui {

namespace {

struct ToggleLook {
    uint8_t edge;      // brackets / ends
    uint8_t text;      // the label
    bool    hot, down, focused, dis;
};

ToggleLook look(Context& ctx, Id id) {
    ToggleLook k;
    k.dis = ctx.disabled();
    k.down = ctx.isPressed(id);
    k.hot = ctx.isHot(id) && (!ctx.isActive(id) || k.down);
    k.focused = ctx.isFocused(id);
    if (ctx.isHot(id) && !k.dis) ctx.setCursor(Cursor::Pointer);
    k.edge = k.dis ? (uint8_t)TEXT_MUTED : k.focused ? (uint8_t)BORDER_FOCUS
           : k.hot ? (uint8_t)ACCENT : (uint8_t)TEXT_MUTED;
    k.text = k.dis ? (uint8_t)TEXT_MUTED : (uint8_t)TEXT;
    return k;
}

/** "[m] Label" / "(m) Label" — returns true on a click. */
bool boxed(Context& ctx, const char* label, uint32_t open, uint32_t close,
           uint32_t mark, uint8_t markSlot) {
    CellBuffer& cb = ctx.cells();
    const Rect r = ctx.allocate(4 + Utf8Width(label), 1);
    const Id id = ctx.makeId(label);
    ctx.focusable(id);
    const bool clicked = ctx.interact(id, r);
    const ToggleLook k = look(ctx, id);

    cb.put(r.x, r.y, open, k.edge, BG_PANEL);
    if (k.down) cb.put(r.x + 1, r.y, mark, TEXT_INV, BG_SEL, ATTR_BOLD);
    else        cb.put(r.x + 1, r.y, mark, k.dis ? (uint8_t)TEXT_MUTED : markSlot, BG_PANEL, ATTR_BOLD);
    cb.put(r.x + 2, r.y, close, k.edge, BG_PANEL);
    cb.text(r.x + 4, r.y, label, k.text, BG_PANEL);
    if (k.focused && ctx.blinkOn()) cb.put(r.x, r.y, Gly::DIAMOND, BORDER_FOCUS, BG_PANEL);
    return clicked;
}

} // namespace

bool Checkbox(Context& ctx, const char* label, bool& v) {
    if (!boxed(ctx, label, '[', ']', v ? Gly::CHECK : (uint32_t)' ', ACCENT)) return false;
    v = !v;
    return true;
}

bool CheckboxTri(Context& ctx, const char* label, int& v) {
    const uint32_t mark = v == 1 ? Gly::CHECK : v == 2 ? Gly::TILDE : (uint32_t)' ';
    if (!boxed(ctx, label, '[', ']', mark, v == 2 ? (uint8_t)WARN : (uint8_t)ACCENT)) return false;
    // Mixed resolves to ON: the user clicked a box that was partly set, and
    // "all of it" is the reading that never loses what was already there.
    v = (v == 1) ? 0 : 1;
    return true;
}

bool Radio(Context& ctx, const char* label, int& sel, int value) {
    const bool on = sel == value;
    ctx.pushId(value);
    const bool clicked = boxed(ctx, label, '(', ')', on ? Gly::DOT_FILL : (uint32_t)' ', ACCENT);
    ctx.popId();
    if (!clicked || on) return false;
    sel = value;
    return true;
}

bool Switch(Context& ctx, const char* label, bool& v) {
    CellBuffer& cb = ctx.cells();
    const char* sOn = str::T(str::ID_ON);
    const char* sOff = str::T(str::ID_OFF);
    const int sw = std::max(Utf8Width(sOn), Utf8Width(sOff));
    const int lw = label ? Utf8Width(label) : 0;
    constexpr int kTrack = 3;                    // cells
    const Rect r = ctx.allocate(kTrack + 1 + sw + (lw ? 2 + lw : 0), 1);
    const Id id = ctx.makeId(label ? label : "switch");
    ctx.focusable(id);
    const bool clicked = ctx.interact(id, r);
    const ToggleLook k = look(ctx, id);

    // The progress bar's shape (§8.3): a `dither` track of the same height
    // and corners, and a thumb over half of it — on the right in `success`
    // when on, on the left and muted when off. Hover and focus light the
    // thumb the way they light every other control's edge.
    const float y0 = r.y + (1.0f - BAR_H) * 0.5f;
    const float half = kTrack * 0.5f;
    const uint8_t thumb = k.dis ? (uint8_t)TEXT_MUTED : k.down ? (uint8_t)TEXT
                        : v ? (uint8_t)SUCCESS : (k.hot || k.focused) ? k.edge : (uint8_t)TEXT_MUTED;
    cb.shape((float)r.x, y0, (float)kTrack, BAR_H, DITHER, BAR_R);
    cb.shape(v ? r.x + half : (float)r.x, y0, half, BAR_H, thumb, BAR_R);
    // Keyboard focus: a 1 px ring round the track, blinking like the ◆ of
    // the other toggles — a glyph over the track would sit on the shape.
    if (k.focused && ctx.blinkOn()) {
        const float px = 1.0f / ctx.input().cell_w, py = 1.0f / ctx.input().cell_h;
        cb.shape(r.x - px, y0 - py, kTrack + 2 * px, BAR_H + 2 * py, BORDER_FOCUS, BAR_R, 1.0f, 1.0f);
    }
    cb.text(r.x + kTrack + 1, r.y, v ? sOn : sOff, v && !k.dis ? (uint8_t)SUCCESS : (uint8_t)TEXT_MUTED,
            BG_PANEL, v ? ATTR_BOLD : ATTR_NONE);
    if (lw) cb.text(r.x + kTrack + 1 + sw + 2, r.y, label, k.text, BG_PANEL);

    if (!clicked) return false;
    v = !v;
    return true;
}

} // namespace ui
