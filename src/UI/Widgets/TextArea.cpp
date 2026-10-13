#include "Widgets.hpp"
#include "TextEdit.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

// The multi-line field (§8.13):
//
//   ┌─[ NOTES ]──────────────────────────────────┐
//   │   1  First line of the document.         ▮ │
//   │   2  Second line, cursor is here█        ▮ │
//   │   3                                        │
//   │      ~                                     │   ← past the end (vi)
//   ├────────────────────────────────────────────┤
//   │ LN 2, COL 26          UTF-8   LF   3 lines │   ← its own status bar
//   └────────────────────────────────────────────┘
//
// No wrapping: a line longer than the view scrolls sideways with the cursor,
// which is what a code editor does and what keeps line numbers meaningful.

namespace ui {

namespace {

int digitsOf(int n) {
    int d = 1;
    while (n >= 10) { n /= 10; ++d; }
    return d;
}

} // namespace

int TextArea(Context& ctx, const char* name, const char* title, const Rect& r, std::string& s) {
    if (r.w < 14 || r.h < 6) return 0;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    ctx.pushId(name);
    const Id id = ctx.makeId("text");

    edit::State& st = ctx.blob<edit::State>(id);
    edit::Text t = Utf8Decode(s.c_str());
    edit::clamp(t, st);

    // The text right under the title, as in every section (§8.1).
    const int viewRows = r.h - 4;
    const int textTop = r.y + 1;
    const int barX = r.x + r.w - 2;
    // Gutter: one margin column after the frame (like every panel), exactly as
    // many digits as the last line number needs, then two blank columns.
    auto gutterFor = [](int lines) { return 1 + digitsOf(lines) + 2; };
    int lines = edit::lineCount(t);
    const int gx = r.x + 1 + gutterFor(lines);
    const int viewW = std::max(1, barX - gx);
    const Rect textRect{ r.x + 1, textTop, r.w - 3, viewRows };

    ctx.focusable(id);
    ctx.interact(id, textRect);
    const bool dis = ctx.disabled();
    const bool focused = ctx.isFocused(id) && !dis;
    int result = 0;
    bool follow = false;                       // scroll to the cursor this frame

    // ---- the pointer --------------------------------------------------------
    auto posAtMouse = [&]() {
        const int line = st.scroll_y + (int)std::floor(in.mouse_y - (float)textTop);
        const int col = st.scroll_x + (int)std::floor(in.mouse_x - (float)gx + 0.5f);
        return edit::posAt(t, std::max(0, line), std::max(0, col));
    };
    if (edit::handleMouse(ctx, id, t, st, posAtMouse)) follow = true;
    if (ctx.isHot(id) && !dis) ctx.setCursor(Cursor::IBeam);

    // ---- the keyboard -------------------------------------------------------
    if (focused) {
        ctx.captureText();
        const edit::Result er = edit::handleKeys(ctx, t, st, true, viewRows);
        if (er.changed) { result |= TXT_CHANGED; s = edit::encode(t); }
        follow = follow || er.changed || er.moved;
    }
    lines = edit::lineCount(t);

    // The wheel moves the VIEW, not the cursor — reading ahead without losing
    // your place. Only typing or moving the cursor brings the view back.
    if (textRect.contains(in.mouse_x, in.mouse_y)) {
        const float w = ctx.takeWheel();
        if (w != 0.0f) st.scroll_y -= (int)std::lround(w * 3.0f);
    }
    int cl = 0, cc = 0;
    edit::lineCol(t, st.cursor, cl, cc);
    if (follow) {
        if (cl < st.scroll_y) st.scroll_y = cl;
        if (cl >= st.scroll_y + viewRows) st.scroll_y = cl - viewRows + 1;
        if (cc < st.scroll_x) st.scroll_x = cc;
        if (cc > st.scroll_x + viewW - 1) st.scroll_x = cc - viewW + 1;
    }
    st.scroll_y = std::clamp(st.scroll_y, 0, std::max(0, lines - viewRows));
    st.scroll_x = std::max(0, st.scroll_x);

    // ---- the frame, the separator, the status bar ------------------------------
    const uint8_t edge = focused ? (uint8_t)BORDER_FOCUS : (uint8_t)BORDER;
    DrawFrame(cb, r, edge, BG_PANEL);
    DrawPanelTitle(cb, r, title, edge);
    const int sepY = r.y + r.h - 3;
    cb.put(r.x, sepY, Gly::TEE_R, edge, BG_PANEL);
    for (int i = r.x + 1; i < r.x + r.w - 1; ++i) cb.put(i, sepY, Gly::H, edge, BG_PANEL);
    cb.put(r.x + r.w - 1, sepY, Gly::TEE_L, edge, BG_PANEL);

    {
        const int sy = r.y + r.h - 2;
        char buf[64];
        std::snprintf(buf, sizeof buf, str::T(str::ID_LN_COL), cl + 1, cc + 1);
        const int lw = cb.text(r.x + 2, sy, buf, TEXT2, BG_PANEL);
        char right[48];
        std::snprintf(right, sizeof right, str::T(str::ID_N_LINES), lines);
        const int rw = Utf8Width(right);
        cb.textRight(r.x + r.w - 3, sy, right, TEXT_MUTED, BG_PANEL);
        std::string enc = "UTF-8  LF";
        if (st.overwrite) { enc += "  "; enc += str::T(str::ID_OVR); }
        const int ew = Utf8Width(enc.c_str());
        // Centred in what is left; dropped when there is no room for it.
        const int free0 = r.x + 2 + lw + 2, free1 = r.x + r.w - 3 - rw - 2;
        if (free1 - free0 + 1 >= ew)
            cb.text(free0 + (free1 - free0 + 1 - ew) / 2, sy, enc.c_str(), TEXT_MUTED, BG_PANEL);
    }

    // ---- the text ---------------------------------------------------------------
    ctx.pushRegion(Rect{ r.x + 1, textTop, r.w - 2, viewRows });
    const int digits = digitsOf(lines);
    const int a = edit::selMin(st), b = edit::selMax(st);
    int pos = edit::posAt(t, st.scroll_y, 0);
    const int n = (int)t.size();
    for (int row = 0; row < viewRows; ++row) {
        const int line = st.scroll_y + row;
        const int y = textTop + row;
        if (line >= lines) {
            cb.put(gx, y, Gly::TILDE, TEXT_MUTED, BG_PANEL);      // vi: past the end
            continue;
        }
        char num[16];
        std::snprintf(num, sizeof num, "%*d", digits, line + 1);
        cb.text(r.x + 2, y, num, line == cl && focused ? (uint8_t)TEXT2 : (uint8_t)TEXT_MUTED,
                BG_PANEL);
        const int end = edit::lineEnd(t, pos);
        for (int c = 0; c < viewW; ++c) {
            const int i = pos + st.scroll_x + c;
            if (i >= end) {
                // A selected line break shows as one inverted cell, so a
                // selection across lines reads as continuous.
                if (i == end && end < n && focused && i >= a && i < b)
                    cb.put(gx + c, y, ' ', TEXT_INV, BG_SEL);
                break;
            }
            const bool sel = focused && i >= a && i < b;
            cb.put(gx + c, y, t[(size_t)i],
                   sel ? (uint8_t)TEXT_INV : dis ? (uint8_t)TEXT_MUTED : (uint8_t)TEXT,
                   sel ? (uint8_t)BG_SEL : (uint8_t)BG_PANEL);
        }
        if (focused && line == cl && edit::cursorVisible(ctx, st)) {
            const int c = cc - st.scroll_x;
            if (c >= 0 && c < viewW) edit::drawCursor(cb, gx + c, y, t, st.cursor, st.overwrite);
        }
        pos = end + 1;
    }
    ctx.popRegion();

    int off = st.scroll_y;
    ScrollbarV(ctx, "bar", barX, textTop, viewRows, lines, viewRows, off,
               ScrollLive(ctx, r, focused));
    st.scroll_y = off;

    ctx.popId();
    return result;
}

} // namespace ui
