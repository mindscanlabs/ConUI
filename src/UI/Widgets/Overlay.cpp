#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Tooltip (§8.11), dropdown (§8.6) and modal (§8.9). The context menu, which
// has a small state machine of its own, lives in Menu.cpp.

namespace ui {

namespace {

std::vector<std::string> splitLines(const char* s) {
    std::vector<std::string> out(1);
    for (const char* p = s; p && *p; ++p) {
        if (*p == '\n') out.emplace_back();
        else out.back() += *p;
    }
    return out;
}

/** Places a w×h box at (x, y), flipping it to stay on screen. */
void fitOnScreen(const CellBuffer& cb, int& x, int& y, int w, int h, int flipX, int flipY) {
    if (x + w > cb.width())  x = flipX - w;
    if (y + h > cb.height()) y = flipY - h;
    x = std::max(0, std::min(x, cb.width() - w));
    y = std::max(0, std::min(y, cb.height() - h));
}

struct DropState {
    bool open = false;
    int  hl = 0;          // the highlighted row while open
};

} // namespace

// ---- tooltip -------------------------------------------------------------------

void Tooltip(Context& ctx, const char* text) {
    const Id id = ctx.lastId();
    if (!text || !*text) return;
    const double hovered = ctx.hoverSeconds(id);
    if (hovered < 0.45) {
        // Sitting still over a widget is not an event: ask for the frame the
        // tooltip is due on, or it never appears until the pointer moves.
        if (hovered > 0.0) ctx.animate(0.45 - hovered);
        return;
    }
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();

    const std::vector<std::string> lines = splitLines(text);
    int tw = 0;
    for (const std::string& l : lines) tw = std::max(tw, Utf8Width(l.c_str()));
    const int w = tw + 4, h = (int)lines.size() + 2;

    // Pressed against the pointer, below and to the right; flipped at the
    // edges so it never runs off the window (§8.11).
    const int mx = (int)std::floor(in.mouse_x), my = (int)std::floor(in.mouse_y);
    int x = mx + 1, y = my + 1;
    fitOnScreen(cb, x, y, w, h, mx, my);

    // Not blocking: a tooltip that took the pointer would take the hover
    // that keeps it on screen, and flicker.
    ctx.beginOverlay(Rect{ x, y, w, h }, /*blocking=*/false);
    const Rect box{ x, y, w, h };
    DrawFrame(cb, box, WARN, BG_RAISED);
    for (size_t i = 0; i < lines.size(); ++i)
        cb.text(x + 2, y + 1 + (int)i, lines[i].c_str(), i == 0 ? (uint8_t)TEXT : (uint8_t)TEXT2,
                BG_RAISED);
    ctx.endOverlay();
}

// ---- dropdown ----------------------------------------------------------------------

bool Dropdown(Context& ctx, const char* name, int w, const char* const* items, int n, int& sel) {
    const Rect r = ctx.allocate(w, 1);
    if (w < 5 || n <= 0) return false;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    const Id id = ctx.makeId(name);
    DropState& st = ctx.blob<DropState>(id);
    sel = std::clamp(sel, 0, n - 1);

    ctx.focusable(id);
    const bool clicked = ctx.interact(id, r);
    const bool dis = ctx.disabled();
    if (ctx.isHot(id) && !dis) ctx.setCursor(Cursor::Pointer);
    // The RAW focus: while the list is open it has claimed the keyboard, and
    // isFocused() says no to every content widget — this one included.
    const bool focused = ctx.focusedId() == id && !dis;
    const int old = sel;
    auto pick = [&](int i) { sel = std::clamp(i, 0, n - 1); st.open = false; };

    // The pointer toggles; the keyboard opens, and when open, picks.
    const bool keyAct = focused && (in.key_enter || in.key_space);
    if (clicked && in.mouse_released) {
        st.open = !st.open;
        st.hl = sel;
    } else if (keyAct) {
        if (st.open) pick(st.hl);
        else { st.open = true; st.hl = sel; }
    } else if (focused && !st.open) {
        // Closed and focused: ↑/↓ step through the items without opening.
        if (in.key_up)   sel = std::max(0, sel - 1);
        if (in.key_down) sel = std::min(n - 1, sel + 1);
    }

    // ---- the field ----------------------------------------------------------
    cb.fill(r.x, r.y, w, 1, ' ', TEXT, BG_SUNKEN);
    // Drawn focused on either answer: the raw focus (the list is open and has
    // claimed the keys) or isFocused() (which a showcase can force).
    if (focused || (ctx.isFocused(id) && !dis))
        cb.put(r.x, r.y, Gly::LEFT_HALF, BORDER_FOCUS, BG_SUNKEN);
    else if (ctx.isHot(id) && !dis) cb.put(r.x, r.y, Gly::LEFT_HALF, BORDER_DIM, BG_SUNKEN);
    {
        int c = 0;
        for (uint32_t cp : Utf8Decode(items[sel])) {
            if (c >= w - 4) break;
            cb.put(r.x + 1 + c++, r.y, cp, dis ? (uint8_t)TEXT_MUTED : (uint8_t)TEXT, BG_SUNKEN);
        }
    }
    cb.put(r.x + w - 2, r.y, st.open ? Gly::TRI_UP : Gly::TRI_DOWN,
           dis ? (uint8_t)TEXT_MUTED : (uint8_t)ACCENT, BG_SUNKEN, ATTR_BOLD);

    if (!st.open || dis) { st.open = false; return sel != old; }

    // ---- the list, in the overlay ---------------------------------------------
    ctx.claimKeys();              // ↑/↓/Enter/Esc are the list's while it is open
    const int h = n + 2;
    int x = r.x, y = r.y + 1;
    if (y + h + 1 > cb.height()) y = r.y - h;          // no room below: open upwards
    y = std::max(0, y);
    const Rect box{ x, y, w, h };

    ctx.beginOverlay(Rect{ x, y, w, h });
    DrawFrame(cb, box, BORDER_FOCUS, BG_RAISED);
    ctx.pushId(name);
    for (int i = 0; i < n; ++i) {
        const int ry = y + 1 + i;
        const Rect row{ x + 1, ry, w - 2, 1 };
        const Id iid = ctx.makeId(i);
        if (ctx.interact(iid, row)) pick(i);
        if (ctx.isHovered(iid)) st.hl = i;             // the pointer moves the highlight
        const bool hot = st.hl == i;
        if (ctx.isHot(iid)) ctx.setCursor(Cursor::Pointer);
        const uint8_t bg = hot ? (uint8_t)BG_SEL : (uint8_t)BG_RAISED;
        const uint8_t fg = hot ? (uint8_t)TEXT_INV : (uint8_t)TEXT;
        if (hot) {
            cb.put(x + 1, ry, Gly::RIGHT_HALF, BG_SEL, BG_RAISED);
            cb.bgRect(x + 2, ry, w - 4, 1, BG_SEL);
            cb.put(x + w - 2, ry, Gly::LEFT_HALF, BG_SEL, BG_RAISED);
        }
        int c = 0;
        for (uint32_t cp : Utf8Decode(items[i])) {
            if (c >= w - 6) break;
            cb.put(x + 2 + c++, ry, cp, fg, bg, hot ? ATTR_BOLD : ATTR_NONE);
        }
        if (i == sel) cb.put(x + w - 3, ry, Gly::CHECK, hot ? (uint8_t)TEXT_INV : (uint8_t)ACCENT, bg);
    }
    ctx.popId();
    ctx.endOverlay();

    // ---- keys and the click elsewhere -------------------------------------------
    if (focused) {
        if (in.key_up)   st.hl = std::max(0, st.hl - 1);
        if (in.key_down) st.hl = std::min(n - 1, st.hl + 1);
    }
    if (in.key_escape) st.open = false;
    if ((in.mouse_pressed || in.mouse_right_pressed) &&
        !box.contains(in.mouse_x, in.mouse_y) && !r.contains(in.mouse_x, in.mouse_y))
        st.open = false;
    return sel != old;
}

// ---- modal ---------------------------------------------------------------------------

bool BeginModal(Context& ctx, const char* name, const char* title, int w, int h, bool& open,
                int flags) {
    // Whether it was open last frame: a danger modal OPENING jolts the window
    // under it (§9) — once, on the event, not while it stays open.
    struct ModalMemo { bool was_open = false; };
    ModalMemo& memo = ctx.blob<ModalMemo>(ctx.makeId(name) ^ 0xD1B54A32D192ED03ull);
    if (!open) { memo.was_open = false; return false; }
    if (!memo.was_open && (flags & MODAL_DANGER))
        ctx.glitchScreen(Context::ScreenGlitch::UnderOverlay);
    memo.was_open = true;
    CellBuffer& cb = ctx.cells();
    w = std::min(w, cb.width() - 2);
    h = std::min(h, cb.height() - 2);
    const int x = (cb.width() - w) / 2, y = (cb.height() - h) / 2;
    const Rect box{ x, y, w, h };
    const uint8_t edge = (flags & MODAL_DANGER) ? (uint8_t)DANGER : (uint8_t)BORDER_FOCUS;

    ctx.pushId(name);
    ctx.beginModal(Rect{ x, y, w, h });
    // The window behind is dimmed with a solid translucent tint (the theme's
    // own `shadow` alpha), not a dithered glyph (§8.9). beginModal asked for
    // it; the renderer lays it over the finished window and under this box,
    // so the card keeps its full strength and the dimmed window shows through
    // around it, right up to the frame line. The modal is then an ordinary
    // panel: ground cut to that line, title on a cap of its own (§8.1).
    DrawFrame(cb, box, edge, BG_PANEL);
    DrawPanelTitle(cb, box, title, edge, edge == DANGER ? (uint8_t)DANGER : (uint8_t)ACCENT);

    ctx.claimEscape();
    if (ctx.input().key_escape) open = false;       // still drawn this frame; gone the next
    ctx.pushRegion(Rect{ x + 3, y + 2, w - 6, h - 3 });
    return true;
}

void EndModal(Context& ctx) {
    ctx.popRegion();
    ctx.endModal();
    ctx.popId();
}

} // namespace ui
