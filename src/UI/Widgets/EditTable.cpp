#include "Widgets.hpp"
#include "TableParts.hpp"
#include "TextEdit.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// The editable table (§8.19):
//
//   ┌─[ TASKS ]─────────────────────────────── 5 rows ─┐
//   │ TASK        │ OWNER  │ NOTES                  ▲   │
//   │ Glyph atlas │ ceo    │ Rasterise once,          │   ← a row is as tall as
//   │             │        │ cache per codepoint      │     its tallest cell
//   │ ▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔ │   ← 1 px between rows
//   │▐Sidebar     ▌ …      │ …                        │   ← the current cell, inverted
//
// A cell is edited in place with the same editing core as TextInput and
// TextArea (TextEdit.*): typing, selection, the clipboard, Up/Down between
// lines — plus the table's own keys: Enter commits, Shift+Enter breaks the
// line, Tab steps to the next cell, Esc cancels.

namespace ui {

namespace {

constexpr int kMaxLines = 6;           // the tallest a row gets; more lines show …

struct EditTableState {
    int top = 0;                       // first visible row
    bool editing = false;
    int erow = -1, ecol = -1;          // the cell being edited
    edit::Text text;                   // its text while editing
    edit::State es;
};

int linesOf(const std::string& s) {
    int n = 1;
    for (char c : s) if (c == '\n') ++n;
    return n;
}

std::vector<edit::Text> splitLines(const std::string& s) {
    std::vector<edit::Text> out(1);
    for (uint32_t cp : Utf8Decode(s.c_str())) {
        if (cp == '\n') out.emplace_back();
        else out.back().push_back(cp);
    }
    return out;
}

} // namespace

int EditTable(Context& ctx, const char* name, const char* title, const Rect& r,
              const EditColumn* cols, int ncols, int rows,
              std::string* (*cell)(void* user, int row, int col), void* user,
              int& selRow, int& selCol, int& sortCol, bool& sortDesc, int& evRow) {
    evRow = -1;
    if (r.w < 12 || r.h < 6 || ncols <= 0 || !cell) return 0;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    ctx.pushId(name);
    const Id id = ctx.makeId("cells");
    EditTableState& st = ctx.blob<EditTableState>(id);
    int events = 0;

    rows = std::max(0, rows);
    selRow = rows > 0 ? std::clamp(selRow, 0, rows - 1) : -1;
    selCol = std::clamp(selCol, 0, ncols - 1);
    if (st.editing && (st.erow < 0 || st.erow >= rows || st.ecol >= ncols)) st.editing = false;

    // ---- geometry: like Table — inside the frame, minus the scrollbar column
    const int innerX = r.x + 1, innerW = r.w - 3;
    std::vector<int> cw, xs((size_t)ncols);
    table::LayoutColumns(cols, ncols, innerW, cw);
    for (int i = 0, x = innerX; i < ncols; ++i) { xs[(size_t)i] = x; x += cw[(size_t)i] + 1; }
    // The header right under the title, as in every section (§8.1).
    const int headerY = r.y + 1, rowTop = r.y + 2;   // rows right under the header
    const int viewH = std::max(1, r.h - 3);
    const Rect rowsRect{ innerX, rowTop, innerW, viewH };

    ctx.focusable(id);
    ctx.interact(id, rowsRect);
    const bool dis = ctx.disabled();
    const bool focused = ctx.isFocused(id) && !dis;

    // Row heights are asked for many times a frame — layout, pointer, scroll,
    // some of it in loops — so each is worked out once and kept. The row being
    // edited is the exception: its text changes under the keys.
    std::vector<int> heights((size_t)rows, -1);
    auto rowH = [&](int row) {
        int& h = heights[(size_t)row];
        if (h < 0) {
            h = 1;
            for (int c = 0; c < ncols; ++c)
                if (const std::string* s = cell(user, row, c)) h = std::max(h, linesOf(*s));
        }
        const int live = st.editing && row == st.erow ? std::max(h, edit::lineCount(st.text)) : h;
        return std::min(live, kMaxLines);
    };
    // Does `row` fit entirely when the view starts at `top`?
    auto fits = [&](int top, int row) {
        int used = 0;
        for (int k = top; k <= row && k < rows; ++k) used += rowH(k);
        return used <= viewH;
    };
    auto cellAt = [&](float mx, float my, int& orow, int& ocol) {
        orow = ocol = -1;
        int y = rowTop;
        for (int k = st.top; k < rows && y < rowTop + viewH; ++k) {
            const int h = rowH(k);
            if (my >= y && my < y + h) { orow = k; break; }
            y += h;
        }
        for (int c = 0; c < ncols; ++c)
            if (mx >= xs[(size_t)c] && mx < xs[(size_t)c] + cw[(size_t)c] + 1) { ocol = c; break; }
    };
    auto rowY = [&](int row) {                  // top line of a visible row, or -1
        int y = rowTop;
        for (int k = st.top; k < rows && y < rowTop + viewH; ++k) {
            if (k == row) return y;
            y += rowH(k);
        }
        return -1;
    };

    auto commit = [&]() {
        if (!st.editing) return;
        std::string* s = cell(user, st.erow, st.ecol);
        const std::string v = edit::encode(st.text);
        if (s && *s != v) { *s = v; events |= ET_EDITED; evRow = st.erow; heights[(size_t)st.erow] = -1; }
        st.editing = false;
    };
    auto startEdit = [&](bool replace, const std::vector<uint32_t>* typed) {
        if (selRow < 0 || dis) return;
        std::string* s = cell(user, selRow, selCol);
        if (!s) return;                                    // read-only cell
        st.editing = true;
        st.erow = selRow; st.ecol = selCol;
        st.text = replace ? edit::Text{} : Utf8Decode(s->c_str());
        if (typed)
            for (uint32_t cp : *typed) if (cp >= 0x20 && cp != 0x7F) st.text.push_back(cp);
        st.es = edit::State{};
        st.es.cursor = st.es.anchor = (int)st.text.size();
        st.es.moved_at = in.time;
    };
    auto moveSel = [&](int dr, int dc) {
        if (rows <= 0) return;
        selRow = std::clamp(selRow + dr, 0, rows - 1);
        selCol = std::clamp(selCol + dc, 0, ncols - 1);
    };

    // ---- the pointer ------------------------------------------------------------------
    if (ctx.justPressed(id)) {
        int pr = -1, pc = -1;
        cellAt(in.mouse_x, in.mouse_y, pr, pc);
        const bool inEdited = st.editing && pr == st.erow && pc == st.ecol;
        if (inEdited) {
            // A click inside the cell being edited places its cursor.
            const int y0 = rowY(st.erow);
            const int line = st.es.scroll_y + (int)std::floor(in.mouse_y - (float)y0);
            const int col = st.es.scroll_x + (int)std::floor(in.mouse_x - (float)(xs[(size_t)pc] + 1) + 0.5f);
            st.es.cursor = edit::posAt(st.text, std::max(0, line), std::max(0, col));
            if (!in.key_shift) st.es.anchor = st.es.cursor;
            st.es.moved_at = in.time;
        } else if (pr >= 0 && pc >= 0) {
            commit();
            selRow = pr; selCol = pc;
            if (in.mouse_double) startEdit(false, nullptr);
        } else {
            commit();
        }
    }
    if (in.mouse_right_pressed && rowsRect.contains(in.mouse_x, in.mouse_y) &&
        !ctx.blockedAt(in.mouse_x, in.mouse_y)) {
        // The row menu acts on the row under the pointer: select it first.
        int pr = -1, pc = -1;
        cellAt(in.mouse_x, in.mouse_y, pr, pc);
        if (pr >= 0) { commit(); selRow = pr; if (pc >= 0) selCol = pc; ctx.setFocus(id); }
    }
    if (rowsRect.contains(in.mouse_x, in.mouse_y)) {
        const float w = ctx.takeWheel();
        if (w != 0.0f) st.top -= (int)std::lround(w * 2.0f);
    }
    if (st.editing && ctx.isHot(id)) {
        int hr = -1, hc = -1;
        cellAt(in.mouse_x, in.mouse_y, hr, hc);
        if (hr == st.erow && hc == st.ecol) ctx.setCursor(Cursor::IBeam);
    }

    // ---- the keyboard -----------------------------------------------------------------
    bool follow = false;
    if (st.editing && !focused) commit();              // left the table: keep what was typed
    if (focused && st.editing) {
        ctx.captureText();
        ctx.consumeTab();                              // Tab steps between cells while editing
        if (in.key_escape) {
            st.editing = false;                        // cancel: the cell keeps its old text
        } else if (in.key_enter && !in.key_shift) {
            commit();
        } else if (in.key_tab) {
            commit();
            int c = selCol + (in.key_shift ? -1 : 1), rr = selRow;
            if (c >= ncols) { c = 0; rr = std::min(rows - 1, rr + 1); }
            if (c < 0) { c = ncols - 1; rr = std::max(0, rr - 1); }
            selRow = rr; selCol = c;
            startEdit(false, nullptr);
        } else {
            // Shift+Enter reaches the editing core as Enter: a new line.
            edit::handleKeys(ctx, st.text, st.es, true, kMaxLines);
        }
        follow = true;
    } else if (focused && rows > 0) {
        const int page = std::max(1, viewH / 2);
        if (in.key_left)  moveSel(0, -1);
        if (in.key_right) moveSel(0, +1);
        if (in.key_up)    moveSel(-1, 0);
        if (in.key_down)  moveSel(+1, 0);
        if (in.key_pgup)  moveSel(-page, 0);
        if (in.key_pgdn)  moveSel(+page, 0);
        if (in.key_home)  { if (in.key_ctrl) selRow = 0; else selCol = 0; }
        if (in.key_end)   { if (in.key_ctrl) selRow = rows - 1; else selCol = ncols - 1; }
        if (in.key_ctrl && in.key_insert) { events |= ET_INSERT; evRow = selRow + 1; }
        else if (in.key_ctrl && in.key_delete) { events |= ET_DELETE; evRow = selRow; }
        else if (in.key_delete) {
            std::string* s = cell(user, selRow, selCol);
            if (s && !s->empty()) { s->clear(); events |= ET_EDITED; evRow = selRow; heights[(size_t)selRow] = -1; }
        } else if (in.key_enter || in.key_f2) {
            startEdit(false, nullptr);
        } else if (!in.text.empty() && !in.key_ctrl) {
            startEdit(true, &in.text);                 // typing replaces the cell, as in a sheet
            ctx.captureText();
        }
        follow = in.key_left || in.key_right || in.key_up || in.key_down || in.key_pgup ||
                 in.key_pgdn || in.key_home || in.key_end;
    } else if (focused && rows == 0 && in.key_ctrl && in.key_insert) {
        events |= ET_INSERT; evRow = 0;
    }

    // Keep the current row in view.
    st.top = std::clamp(st.top, 0, std::max(0, rows - 1));
    if (follow && selRow >= 0) {
        if (selRow < st.top) st.top = selRow;
        while (st.top < selRow && !fits(st.top, selRow)) ++st.top;
    }

    // ---- the frame ----------------------------------------------------------------------
    const uint8_t edge = focused ? (uint8_t)BORDER_FOCUS : (uint8_t)BORDER;
    table::DrawFrame(cb, r, edge, title, rows, headerY);
    ctx.pushRegion(Rect{ innerX, headerY, innerW, r.h - 2 });

    // ---- header: a sort commits the cell being edited first ----------------------------
    if (table::Header(ctx, cols, ncols, xs, cw, headerY, sortCol, sortDesc)) {
        commit();
        events |= ET_SORT;
    }

    // ---- rows ---------------------------------------------------------------------------
    ctx.pushRegion(rowsRect);
    const int chh = std::max(1, in.cell_h);
    int y = rowTop, fully = 0;
    for (int row = st.top; row < rows && y < rowTop + viewH; ++row) {
        const int h = rowH(row);
        const int vis = std::min(h, rowTop + viewH - y);
        if (vis == h) ++fully;
        for (int c = 0; c < ncols; ++c) {
            const int x0 = xs[(size_t)c], w = cw[(size_t)c];
            const bool isEdit = st.editing && row == st.erow && c == st.ecol;
            const bool isSel = row == selRow && c == selCol;
            const uint8_t bg = isEdit ? (uint8_t)BG_SUNKEN
                             : isSel ? (focused ? (uint8_t)BG_SEL : (uint8_t)BG_RAISED) : (uint8_t)BG_PANEL;
            const uint8_t fg = isSel && focused && !isEdit ? (uint8_t)TEXT_INV
                             : dis ? (uint8_t)TEXT_MUTED : (uint8_t)TEXT;
            if (bg != BG_PANEL) cb.bgRect(x0, y, w, vis, bg);

            if (isEdit) {
                // The cell being edited: the editing core's text, its cursor and
                // selection, scrolled to keep the cursor in the cell.
                int cl = 0, cc = 0;
                edit::lineCol(st.text, st.es.cursor, cl, cc);
                const int viewW = std::max(1, w - 2);
                if (cl < st.es.scroll_y) st.es.scroll_y = cl;
                if (cl >= st.es.scroll_y + h) st.es.scroll_y = cl - h + 1;
                if (cc < st.es.scroll_x) st.es.scroll_x = cc;
                if (cc > st.es.scroll_x + viewW - 1) st.es.scroll_x = cc - viewW + 1;
                const int a = edit::selMin(st.es), b = edit::selMax(st.es);
                const int n = (int)st.text.size();
                int pos = edit::posAt(st.text, st.es.scroll_y, 0);
                for (int li = 0; li < vis; ++li) {
                    const int line = st.es.scroll_y + li;
                    if (line >= edit::lineCount(st.text)) break;
                    const int end = edit::lineEnd(st.text, pos);
                    for (int k = 0; k < viewW; ++k) {
                        const int i = pos + st.es.scroll_x + k;
                        if (i >= end) break;
                        const bool sel = i >= a && i < b;
                        cb.put(x0 + 1 + k, y + li, st.text[(size_t)i],
                               sel ? (uint8_t)TEXT_INV : (uint8_t)TEXT, sel ? (uint8_t)BG_SEL : bg);
                    }
                    if (line == cl && edit::cursorVisible(ctx, st.es)) {
                        const int k = cc - st.es.scroll_x;
                        if (k >= 0 && k < viewW)
                            edit::drawCursor(cb, x0 + 1 + k, y + li, st.text, st.es.cursor, st.es.overwrite);
                    }
                    pos = end + 1;
                    if (pos > n) break;
                }
            } else if (const std::string* s = cell(user, row, c)) {
                const std::vector<edit::Text> lines = splitLines(*s);
                for (int li = 0; li < vis && li < (int)lines.size(); ++li) {
                    edit::Text t = table::Fit(lines[(size_t)li], w - 2);
                    // More lines than the row shows: the last one says so.
                    if (li == h - 1 && (int)lines.size() > h) {
                        if ((int)t.size() >= w - 2 && !t.empty()) t.back() = 0x2026;
                        else t.push_back(0x2026);
                    }
                    int tx = cols[c].right ? x0 + w - 1 - (int)t.size() : x0 + 1;
                    for (uint32_t cp : t) cb.put(tx++, y + li, cp, fg, bg);
                }
            }
            if (c + 1 < ncols)
                for (int li = 0; li < vis; ++li) cb.put(x0 + w, y + li, Gly::V, TABLE_COLSEP, BG_PANEL);
        }
        // A one-pixel line between rows: a multi-line row still reads as one.
        if (vis == h && y + h < rowTop + viewH)
            cb.shape((float)innerX, (float)(y + h) - 1.0f / chh, (float)innerW, 1.0f / chh, TABLE_ROWSEP);
        y += h;
    }
    ctx.popRegion();
    ctx.popRegion();

    // ---- the row menu: the same insert/delete as Ctrl+Insert / Ctrl+Delete -----------
    if (BeginContextMenu(ctx, "rowmenu", rowsRect)) {
        if (MenuItem(ctx, str::T(str::ID_ET_INSERT_ROW), "Ctrl+Ins")) {
            events |= ET_INSERT; evRow = rows > 0 ? selRow + 1 : 0;
        }
        if (MenuItem(ctx, str::T(str::ID_ET_DELETE_ROW), "Ctrl+Del",
                     rows > 0 ? MENU_DANGER : MENU_DISABLED)) {
            events |= ET_DELETE; evRow = selRow;
        }
        EndContextMenu(ctx);
    }

    ScrollbarV(ctx, "vbar", r.x + r.w - 2, rowTop, viewH, rows, std::max(1, fully), st.top,
               ScrollLive(ctx, r, focused));

    ctx.popId();
    return events;
}

} // namespace ui
