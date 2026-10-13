#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// Context menu (§8.10), accelerators on the right:
//
//   ┌──────────────────────────────┐
//   │ Open                   Enter │
//   │▐Rename…                  F2 ▌│  ← inverted under the pointer
//   ├──────────────────────────────┤
//   │ ✓ Show hidden                │
//   │   Sort by                  ▶ │  ← submenu
//   ├──────────────────────────────┤
//   │ Delete                   Del │  ← danger
//   └──────────────────────────────┘
//
// Immediate mode needs the menu's size before its items are drawn, and the
// items are what determine it — so each menu remembers what its items
// measured LAST frame. The first frame after opening only measures.

namespace ui {

namespace {

struct MenuState {
    bool open = false;
    int  x = 0, y = 0;         // where it was asked to open (the pointer)
    int  w = 0;                // widest item, content cells, from last frame
    int  rows = 0;             // item + separator rows, from last frame
    int  hl = -1;              // keyboard highlight
    int  child = -1;           // row of the open submenu, -1 none
    bool opened_now = false;   // opened this frame: no "click outside" yet
    float mx = -1.0f, my = -1.0f;   // the pointer last frame
};

struct MenuFrame {
    MenuState* st;
    Rect box;                  // the frame, shadow excluded
    int  row = 0;              // next row to lay out
    int  measure_w = 0;        // what this frame's items need
    bool measuring = false;
    std::vector<bool> selectable;
    int  child_at_begin = -1;  // the open submenu as the frame started
};

// Menus nest (submenus) and a chain is drawn start to finish inside one
// frame of one window, so a stack serves every context.
std::vector<MenuFrame> g_menus;
// The boxes of the chain being drawn, for "was the click outside all of it".
std::vector<Rect> g_boxes;

MenuFrame* top() { return g_menus.empty() ? nullptr : &g_menus.back(); }

/** Opens the menu frame for `st` whose top-left wants to be at (x, y). */
void beginMenuAt(Context& ctx, MenuState& st, int x, int y, int flipX, int flipY) {
    CellBuffer& cb = ctx.cells();
    MenuFrame f;
    f.st = &st;
    f.measuring = st.w == 0;
    f.child_at_begin = st.child;
    const int bw = st.w + 4, bh = st.rows + 2;
    if (x + bw + 1 > cb.width())  x = flipX - bw;      // no room: to the other side
    if (y + bh + 1 > cb.height()) y = flipY - bh + 1;
    x = std::max(0, std::min(x, cb.width() - bw - 1));
    y = std::max(0, std::min(y, cb.height() - bh - 1));
    f.box = Rect{ x, y, bw, bh };
    if (!f.measuring) {
        ctx.beginOverlay(Rect{ x, y, bw, bh });
        DrawFrame(cb, f.box, BORDER_FOCUS, BG_RAISED);
        g_boxes.push_back(f.box);
    }
    ctx.claimKeys();
    g_menus.push_back(f);
}

/** Closes the frame; returns the state of the menu that owned it. */
MenuState* endMenu(Context& ctx) {
    if (g_menus.empty()) return nullptr;
    MenuFrame f = g_menus.back();
    g_menus.pop_back();
    MenuState& st = *f.st;
    st.w = std::max(f.measure_w, 1);
    st.rows = f.row;
    st.mx = ctx.input().mouse_x;
    st.my = ctx.input().mouse_y;
    if (!f.measuring) ctx.endOverlay();

    // Keys go to the innermost open menu only — the one with no open child.
    const Input& in = ctx.input();
    if (st.child < 0 && !f.measuring) {
        auto step = [&](int d) {
            const int n = (int)f.selectable.size();
            if (n == 0) return;
            int i = st.hl < 0 ? (d > 0 ? -1 : n) : st.hl;
            for (int k = 0; k < n; ++k) {
                i = (i + d + n) % n;
                if (f.selectable[(size_t)i]) { st.hl = i; return; }
            }
        };
        if (in.key_down) step(+1);
        if (in.key_up)   step(-1);
    }
    return &st;
}

MenuFrame* itemRow(int& row) {
    MenuFrame* f = top();
    if (!f) return nullptr;
    row = f->row++;
    return f;
}

/** The shared look of a menu row; returns true when it was chosen. */
bool menuRow(Context& ctx, const char* label, const char* right, uint32_t mark,
             int flags, bool submenu, bool* opensChild) {
    int row = 0;
    MenuFrame* f = itemRow(row);
    if (!f) return false;
    const int lw = Utf8Width(label) + 2;                     // "✓ " prefix column
    const int rw = right ? Utf8Width(right) : (submenu ? 1 : 0);
    f->measure_w = std::max(f->measure_w, lw + (rw ? rw + 3 : 0));
    const bool dis = (flags & MENU_DISABLED) != 0;
    f->selectable.push_back(!dis);
    if (f->measuring) return false;

    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    MenuState& st = *f->st;
    const int y = f->box.y + 1 + row;
    const Rect r{ f->box.x + 1, y, f->box.w - 2, 1 };
    const Id id = ctx.makeId(label);

    bool chosen = false;
    if (!dis) {
        chosen = ctx.interact(id, r) && in.mouse_released;
        if (ctx.isHot(id)) ctx.setCursor(Cursor::Pointer);
        // Only a pointer that MOVED takes the highlight: one resting over the
        // menu must not undo every ↑/↓ the keyboard makes.
        if (ctx.isHovered(id) && (in.mouse_x != st.mx || in.mouse_y != st.my)) {
            st.hl = row;
            // Hovering another row closes an open submenu; hovering the
            // submenu's own row opens it.
            st.child = submenu ? row : -1;
        }
    }
    const bool innermost = st.child < 0 || (submenu && st.child == row);
    if (!dis && st.hl == row && innermost) {
        if (in.key_enter || (submenu && in.key_right)) {
            if (submenu) st.child = row;
            else chosen = true;
        }
    }
    if (submenu && opensChild) *opensChild = st.child == row;

    const bool hot = !dis && (st.hl == row || (submenu && st.child == row));
    const uint8_t bg = hot ? (uint8_t)BG_SEL : (uint8_t)BG_RAISED;
    const uint8_t fg = dis ? (uint8_t)TEXT_MUTED : hot ? (uint8_t)TEXT_INV
                     : (flags & MENU_DANGER) ? (uint8_t)DANGER : (uint8_t)TEXT;
    if (hot) {
        cb.put(r.x, y, Gly::RIGHT_HALF, BG_SEL, BG_RAISED);
        cb.bgRect(r.x + 1, y, r.w - 2, 1, BG_SEL);
        cb.put(r.x + r.w - 1, y, Gly::LEFT_HALF, BG_SEL, BG_RAISED);
    }
    if (mark) cb.put(r.x + 1, y, mark, hot ? (uint8_t)TEXT_INV : (uint8_t)ACCENT, bg, ATTR_BOLD);
    // Every label starts after the ✓ column, marked or not, so a menu that
    // mixes checks and plain items still reads as one aligned column.
    cb.text(r.x + 3, y, label, fg, bg, hot ? ATTR_BOLD : ATTR_NONE);
    if (submenu)
        cb.put(r.x + r.w - 2, y, Gly::TRI_RIGHT, hot ? (uint8_t)TEXT_INV : (uint8_t)ACCENT, bg);
    else if (right)
        cb.textRight(r.x + r.w - 2, y, right, hot ? (uint8_t)TEXT_INV : (uint8_t)TEXT_MUTED, bg);

    if (chosen) {
        // A chosen item closes the whole chain, not just its own menu.
        g_menus.front().st->open = false;
    }
    return chosen;
}

} // namespace

void OpenContextMenu(Context& ctx, const char* name, int x, int y) {
    MenuState& st = ctx.blob<MenuState>(ctx.makeId(name));
    st.open = true;
    st.x = x;
    st.y = y;
    st.hl = -1;
    st.child = -1;
    st.opened_now = true;
}

bool BeginContextMenu(Context& ctx, const char* name, const Rect& area) {
    const Input& in = ctx.input();
    const Id id = ctx.makeId(name);
    MenuState& st = ctx.blob<MenuState>(id);
    if (in.mouse_right_pressed && area.contains(in.mouse_x, in.mouse_y) &&
        !ctx.blockedAt(in.mouse_x, in.mouse_y))
        OpenContextMenu(ctx, name, (int)std::floor(in.mouse_x), (int)std::floor(in.mouse_y));
    if (!st.open) return false;
    ctx.pushId(name);
    g_boxes.clear();
    beginMenuAt(ctx, st, st.x, st.y, st.x, st.y);
    return true;
}

void EndContextMenu(Context& ctx) {
    const bool measuring = top() && top()->measuring;
    // Esc closes the INNERMOST menu: if a submenu was open as this frame
    // began, the Esc was its, even though it has already closed by now.
    const int childBefore = top() ? top()->child_at_begin : -1;
    MenuState* st = endMenu(ctx);
    ctx.popId();
    if (!st) return;
    const Input& in = ctx.input();
    if (in.key_escape && childBefore < 0) st->open = false;
    // A press anywhere outside the chain closes it — except on the frame it
    // opened, when the press that opened it is still this frame's press.
    if (!st->opened_now && !measuring && (in.mouse_pressed || in.mouse_right_pressed)) {
        bool inside = false;
        for (const Rect& b : g_boxes) inside = inside || b.contains(in.mouse_x, in.mouse_y);
        if (!inside) st->open = false;
    }
    st->opened_now = false;
    if (!st->open) { st->hl = -1; st->child = -1; }
    g_boxes.clear();
}

bool MenuItem(Context& ctx, const char* label, const char* shortcut, int flags) {
    return menuRow(ctx, label, shortcut, 0, flags, false, nullptr);
}

bool MenuCheck(Context& ctx, const char* label, bool& v) {
    if (!menuRow(ctx, label, nullptr, v ? Gly::CHECK : 0, MENU_NONE, false, nullptr)) return false;
    v = !v;
    return true;
}

void MenuSeparator(Context& ctx) {
    int row = 0;
    MenuFrame* f = itemRow(row);
    if (!f) return;
    f->selectable.push_back(false);
    if (f->measuring) return;
    CellBuffer& cb = ctx.cells();
    const int y = f->box.y + 1 + row;
    cb.put(f->box.x, y, Gly::TEE_R, BORDER_FOCUS, BG_RAISED);
    for (int i = f->box.x + 1; i < f->box.x + f->box.w - 1; ++i)
        cb.put(i, y, Gly::H, BORDER_DIM, BG_RAISED);
    cb.put(f->box.x + f->box.w - 1, y, Gly::TEE_L, BORDER_FOCUS, BG_RAISED);
}

bool BeginSubmenu(Context& ctx, const char* label) {
    MenuFrame* parent = top();
    if (!parent) return false;
    bool open = false;
    const int row = parent->row;
    menuRow(ctx, label, nullptr, 0, MENU_NONE, true, &open);
    if (parent->measuring || !open) return false;

    // The child opens beside its row; with no room on the right it opens on
    // the left of the parent instead of running off the window.
    const Rect pb = parent->box;
    ctx.pushId(label);
    MenuState& st = ctx.blob<MenuState>(ctx.makeId("sub"));
    st.open = true;
    beginMenuAt(ctx, st, pb.x + pb.w, pb.y + row, pb.x, pb.y + row + 1);
    return true;
}

void EndSubmenu(Context& ctx) {
    // The parent is still below it on the stack; ← or Esc closes the child —
    // unless the child had a child of its own open, which took the key.
    const int childBefore = top() ? top()->child_at_begin : -1;
    MenuState* st = endMenu(ctx);
    ctx.popId();
    MenuFrame* parent = top();
    if (!st || !parent) return;
    const Input& in = ctx.input();
    if (childBefore < 0 && (in.key_left || in.key_escape)) {
        parent->st->child = -1;
        st->hl = -1;
    }
    if (!parent->st->open) { st->hl = -1; st->child = -1; }
}

// ---- the menu bar ------------------------------------------------------------------

namespace {
struct MenuBarState { Id open = kNoId; };   // the bar menu that is open, if any
}

void MenuBarItem(Context& ctx, const char* label, const char* menuName) {
    CellBuffer& cb = ctx.cells();
    const Rect r = ctx.allocate(Utf8Width(label) + 2, 1);        // " LABEL "
    const Id menuId = ctx.makeId(menuName);
    MenuState& st = ctx.blob<MenuState>(menuId);
    MenuBarState& bar = ctx.blob<MenuBarState>(ctx.makeId("#menubar"));
    if (bar.open != kNoId && !ctx.blob<MenuState>(bar.open).open) bar.open = kNoId;

    ctx.pushId("#menubar");
    const Id id = ctx.makeId(menuName);
    ctx.popId();
    ctx.interact(id, r);

    auto open = [&]() {
        if (bar.open != kNoId && bar.open != menuId) ctx.blob<MenuState>(bar.open).open = false;
        OpenContextMenu(ctx, menuName, r.x, r.y + 1);
        bar.open = menuId;
    };
    if (ctx.justPressed(id)) {
        if (st.open) { st.open = false; bar.open = kNoId; }
        else open();
    } else if (ctx.isHovered(id) && bar.open != kNoId && bar.open != menuId) {
        // A menu of the bar is already open: moving along the bar walks the
        // menus, the way every desktop menu bar behaves.
        open();
    }

    const bool on = st.open;
    const bool hot = ctx.isHot(id);
    if (hot) ctx.setCursor(Cursor::Pointer);
    const uint8_t bg = on ? (uint8_t)BG_SEL : (uint8_t)BG_WINDOW;
    const uint8_t fg = on ? (uint8_t)TEXT_INV : hot ? (uint8_t)ACCENT : (uint8_t)TEXT;
    cb.put(r.x, r.y, ' ', fg, bg);
    cb.text(r.x + 1, r.y, label, fg, bg, on ? ATTR_BOLD : ATTR_NONE);
    cb.put(r.x + r.w - 1, r.y, ' ', fg, bg);
}

} // namespace ui
