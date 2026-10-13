#include "Widgets.hpp"
#include "TableParts.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// The table (§8.8):
//
//   ┌─[ PROCESSES ]───────────────────────── 124 rows ─┐
//   │ PID    │ NAME              │ CPU ▼ │ MEM      │ ST ▮│
//   │  1042  │ conui.exe         │ 12.4% │  38.2 MB │ RU ▮│
//   │▐ 2288  │ build.exe         │  8.1% │ 112.0 MB │ RU▌ │   ← selection
//   └───────────────────────────────────────────────────┘
//
// The header is `bg.raised`, numbers are right-aligned, the sort arrow sits in
// the header of the sorted column. The table never sorts anything itself.
// TBL_BARE drops the frame: the header is the top row of the rectangle.

namespace ui {

// ---- the parts EditTable shares (TableParts.hpp) -----------------------------------

namespace table {

std::vector<uint32_t> Fit(std::vector<uint32_t> t, int w) {
    if ((int)t.size() > w) {
        t.resize((size_t)std::max(0, w));
        if (w > 0) t.back() = 0x2026;
    }
    return t;
}

int LayoutColumns(const TableColumn* cols, int n, int innerW, std::vector<int>& cw) {
    cw.assign((size_t)n, 0);
    int total = n - 1;                                  // the │ separators
    for (int i = 0; i < n; ++i) { cw[(size_t)i] = cols[i].width + 2; total += cw[(size_t)i]; }
    if (total < innerW) { cw.back() += innerW - total; total = innerW; }   // the last one stretches
    return total;
}

void DrawFrame(CellBuffer& cb, const Rect& r, uint8_t edge, const char* title, int rows,
               int headerY) {
    ui::DrawFrame(cb, r, edge, BG_PANEL);
    const int used = DrawPanelTitle(cb, r, title, edge);
    char count[32];
    std::snprintf(count, sizeof count, " %d %s ", rows, str::T(str::ID_ROWS));
    if (used + Utf8Width(count) + 4 <= r.w) cb.textRight(r.x + r.w - 3, r.y, count, TEXT_MUTED, BG_PANEL);
    cb.bgRect(r.x + 1, headerY, r.w - 2, 1, BG_RAISED);
}

bool Header(Context& ctx, const TableColumn* cols, int n, const std::vector<int>& xs,
            const std::vector<int>& cw, int headerY, int& sortCol, bool& sortDesc) {
    CellBuffer& cb = ctx.cells();
    bool sorted = false;
    ctx.pushId("hdr");
    for (int i = 0; i < n; ++i) {
        const Rect hr{ xs[(size_t)i], headerY, cw[(size_t)i], 1 };
        const Id hid = ctx.makeId(i);
        if (ctx.interact(hid, hr)) {
            // A new column sorts ascending; the same column again flips it.
            if (sortCol == i) sortDesc = !sortDesc;
            else { sortCol = i; sortDesc = false; }
            sorted = true;
        }
        const bool hot = ctx.isHot(hid);
        if (hot) ctx.setCursor(Cursor::Pointer);
        // Header labels are always left-aligned, whatever the column's data
        // alignment is; the sort arrow sits at the column's right edge (§8.8).
        const bool arrow = sortCol == i;
        int tx = hr.x + 1;
        for (uint32_t cp : Fit(Utf8Decode(cols[i].title ? cols[i].title : ""), hr.w - 2 - (arrow ? 2 : 0)))
            cb.put(tx++, headerY, cp, hot ? (uint8_t)ACCENT : (uint8_t)TEXT, BG_RAISED, ATTR_BOLD);
        if (arrow) cb.put(hr.x + hr.w - 2, headerY, sortDesc ? Gly::TRI_DOWN : Gly::TRI_UP, ACCENT, BG_RAISED);
        // The column separator runs on into the rows below: the header is
        // told apart by its raised ground, not by a rule under it.
        if (i + 1 < n) cb.put(hr.x + hr.w, headerY, Gly::V, TABLE_COLSEP, BG_RAISED);
    }
    ctx.popId();
    return sorted;
}

} // namespace table

// ---- the table ------------------------------------------------------------------------

namespace {

struct TableState {
    int scroll_y = 0;
    int scroll_x = 0;
};

} // namespace

int Table(Context& ctx, const char* name, const char* title, const Rect& r,
          const TableColumn* cols, int ncols, int rows,
          const char* (*cell)(void* user, int row, int col), void* user,
          int& selected, int& sortCol, bool& sortDesc, int flags) {
    const bool bare = (flags & TBL_BARE) != 0;
    if (r.w < 10 || r.h < (bare ? 3 : 4) || ncols <= 0) return 0;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    ctx.pushId(name);
    const Id id = ctx.makeId("rows");
    TableState& st = ctx.blob<TableState>(id);
    int events = 0;

    // ---- geometry --------------------------------------------------------------
    // Inside the frame, minus the scrollbar column on the right. Bare: the
    // whole rect, the scrollbar in its last column.
    const int innerX = bare ? r.x : r.x + 1;
    const int innerW = bare ? r.w - 1 : r.w - 3;
    std::vector<int> cw;
    const int total = table::LayoutColumns(cols, ncols, innerW, cw);
    const bool hOverflow = total > innerW;
    // The header right under the title, as in every section (§8.1). Bare:
    // no frame — the header is the top row, and the bottom row is the
    // table's own margin, since no frame keeps it off whatever is below; the
    // horizontal scrollbar, when there is one, lives in that row.
    const int headerY = bare ? r.y : r.y + 1;
    const int rowTop = headerY + 1;          // rows right under the header
    const int viewRows = std::max(1, bare ? r.h - 2 : r.h - 3 - (hOverflow ? 1 : 0));
    const Rect rowsRect{ innerX, rowTop, innerW, viewRows };

    ctx.focusable(id);
    ctx.interact(id, rowsRect);
    const bool focused = ctx.isFocused(id) && !ctx.disabled();

    // ---- input: rows -------------------------------------------------------------
    bool follow = false;
    auto select = [&](int row) {
        row = std::clamp(row, 0, std::max(0, rows - 1));
        if (rows > 0 && row != selected) { selected = row; events |= TBL_SELECT; }
        follow = true;
    };
    if (ctx.justPressed(id)) {
        const int row = st.scroll_y + (int)std::floor(in.mouse_y - (float)rowTop);
        if (row >= 0 && row < rows) {
            select(row);
            if (in.mouse_double) events |= TBL_ACTIVATE;
        }
    }
    if (focused && rows > 0) {
        const int page = std::max(1, viewRows - 1);
        if (in.key_up)   select(selected < 0 ? 0 : selected - 1);
        if (in.key_down) select(selected < 0 ? 0 : selected + 1);
        if (in.key_pgup) select(selected - page);
        if (in.key_pgdn) select(selected + page);
        if (in.key_home) select(0);
        if (in.key_end)  select(rows - 1);
        if (in.key_enter && selected >= 0) events |= TBL_ACTIVATE;
    }
    if (rowsRect.contains(in.mouse_x, in.mouse_y)) {
        const float w = ctx.takeWheel();
        if (w != 0.0f) st.scroll_y -= (int)std::lround(w * 3.0f);
    }
    if (follow && selected >= 0) {
        if (selected < st.scroll_y) st.scroll_y = selected;
        if (selected >= st.scroll_y + viewRows) st.scroll_y = selected - viewRows + 1;
    }
    st.scroll_y = std::clamp(st.scroll_y, 0, std::max(0, rows - viewRows));
    st.scroll_x = std::clamp(st.scroll_x, 0, std::max(0, total - innerW));

    // ---- the frame ---------------------------------------------------------------
    const uint8_t edge = focused ? (uint8_t)BORDER_FOCUS : (uint8_t)BORDER;
    if (bare) {
        // Only the rows' own ground and the raised header over it.
        cb.bgRect(r.x, r.y, r.w, r.h, BG_PANEL);
        cb.bgRect(r.x, headerY, r.w, 1, BG_RAISED);
    } else {
        table::DrawFrame(cb, r, edge, title, rows, headerY);
    }

    // ---- header and rows — all scrolled sideways together ----------------------
    std::vector<int> xs((size_t)ncols);
    for (int i = 0, x = innerX - st.scroll_x; i < ncols; ++i) { xs[(size_t)i] = x; x += cw[(size_t)i] + 1; }
    ctx.pushRegion(Rect{ innerX, headerY, innerW, bare ? r.h : r.h - 2 });

    if (table::Header(ctx, cols, ncols, xs, cw, headerY, sortCol, sortDesc))
        events |= TBL_SORT;

    const bool rowsHot = ctx.isHot(id) && rowsRect.contains(in.mouse_x, in.mouse_y);
    const int hotRow = rowsHot ? st.scroll_y + (int)std::floor(in.mouse_y - (float)rowTop) : -1;
    for (int k = 0; k < viewRows; ++k) {
        const int row = st.scroll_y + k;
        if (row >= rows) break;
        const int y = rowTop + k;
        const bool sel = row == selected;
        const bool hot = row == hotRow && !sel;
        const uint8_t bg = sel ? (uint8_t)BG_SEL : hot ? (uint8_t)BG_RAISED : (uint8_t)BG_PANEL;
        const uint8_t fg = sel ? (uint8_t)TEXT_INV : (uint8_t)TEXT;
        // The bar's rounded ends: text and separators never overwrite them.
        auto isCap = [&](int x) { return (sel || hot) && (x == innerX || x == innerX + innerW - 1); };
        if (sel || hot) {
            // Full-width inversion with half-block caps, so the bar
            // reads as one object rather than as a run of coloured cells.
            cb.bgRect(innerX + 1, y, innerW - 2, 1, bg);
            cb.put(innerX, y, Gly::RIGHT_HALF, bg, BG_PANEL);
            cb.put(innerX + innerW - 1, y, Gly::LEFT_HALF, bg, BG_PANEL);
        }
        for (int i = 0; i < ncols; ++i) {
            const int x0 = xs[(size_t)i];
            const char* s = cell(user, row, i);
            const std::vector<uint32_t> cps = table::Fit(Utf8Decode(s ? s : ""), cw[(size_t)i] - 2);
            int tx = cols[i].right ? x0 + cw[(size_t)i] - 1 - (int)cps.size() : x0 + 1;
            for (uint32_t cp : cps) {
                if (!isCap(tx)) cb.put(tx, y, cp, fg, bg);
                ++tx;
            }
            const int sx = x0 + cw[(size_t)i];
            if (i + 1 < ncols && !isCap(sx))
                cb.put(sx, y, Gly::V, sel ? (uint8_t)TEXT_INV : (uint8_t)TABLE_COLSEP, bg);
        }
    }
    ctx.popRegion();

    // ---- scrollbars ----------------------------------------------------------------
    const bool live = ScrollLive(ctx, r, focused);
    ScrollbarV(ctx, "vbar", bare ? r.x + r.w - 1 : r.x + r.w - 2, rowTop, viewRows, rows, viewRows,
               st.scroll_y, live);
    if (hOverflow)
        ScrollbarH(ctx, "hbar", innerX, bare ? r.y + r.h - 1 : r.y + r.h - 2, innerW, total, innerW,
                   st.scroll_x, live, /*centred=*/bare);

    ctx.popId();
    return events;
}

} // namespace ui
