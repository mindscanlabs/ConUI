#include "Widgets.hpp"
#include "TextEdit.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>

// The single-line field (§8.12):
//
//   NAME   ▌conui_playground█                     ← block cursor
//   SEARCH  ▒search by name…                      ← placeholder
//   PORT    80800                              ✗  ← error (danger)
//   PATH    C:\projects▐conui▌                    ← selection
//
// A strip of `bg.sunken`. The focused field carries a ▌ in
// `border.focus` on its left edge — the same "edge in the focus colour" rule
// as every other widget, with the blinking part left to the cursor.

namespace ui {

int TextInput(Context& ctx, const char* name, int w, std::string& s,
              const char* placeholder, int flags, std::vector<std::string>* history) {
    const Rect r = ctx.allocate(w, 1);
    if (w < 4) return 0;
    CellBuffer& cb = ctx.cells();
    const Id id = ctx.makeId(name);
    ctx.focusable(id);
    ctx.interact(id, r);

    edit::State& st = ctx.blob<edit::State>(id);
    edit::Text t = Utf8Decode(s.c_str());
    edit::clamp(t, st);

    const Input& in = ctx.input();
    const bool dis = ctx.disabled();
    const bool focused = ctx.isFocused(id) && !dis;
    const bool err = (flags & TXT_ERROR) != 0;
    const int tx = r.x + 1;
    const int viewW = std::max(1, w - 2 - (err ? 1 : 0));
    int result = 0;

    // ---- the pointer: place, drag-select, double-click a word -----------
    auto posAtMouse = [&]() {
        const int col = (int)std::floor(in.mouse_x - (float)tx + 0.5f);
        return std::clamp(st.scroll_x + col, 0, (int)t.size());
    };
    edit::handleMouse(ctx, id, t, st, posAtMouse);
    if (ctx.isHot(id) && !dis) ctx.setCursor(Cursor::IBeam);

    // ---- the keyboard ------------------------------------------------------
    if (focused) {
        ctx.captureText();
        const edit::Result er = edit::handleKeys(ctx, t, st, false, 1);
        if (er.changed) { result |= TXT_CHANGED; st.history = -1; }

        // History: ↑ walks back through what was submitted,
        // ↓ walks forward and finally returns what was being typed.
        if (history && !history->empty() && (in.key_up || in.key_down)) {
            const int n = (int)history->size();
            if (st.history < 0) st.draft = edit::encode(t);
            int h = st.history + (in.key_up ? 1 : -1);
            h = std::clamp(h, -1, n - 1);
            if (h != st.history) {
                st.history = h;
                const std::string& src = h < 0 ? st.draft : (*history)[(size_t)(n - 1 - h)];
                t = Utf8Decode(src.c_str());
                st.cursor = st.anchor = (int)t.size();
                st.moved_at = in.time;
                result |= TXT_CHANGED;
            }
        }
        if (in.key_enter) {
            result |= TXT_SUBMITTED;
            if (history && !t.empty()) {
                const std::string cur = edit::encode(t);
                if (history->empty() || history->back() != cur) history->push_back(cur);
            }
            st.history = -1;
        }
        if (result & TXT_CHANGED) s = edit::encode(t);
    }

    // ---- keep the cursor in view -----------------------------------------
    const int n = (int)t.size();
    if (st.cursor < st.scroll_x) st.scroll_x = st.cursor;
    if (st.cursor > st.scroll_x + viewW - 1) st.scroll_x = st.cursor - viewW + 1;
    st.scroll_x = std::clamp(st.scroll_x, 0, std::max(0, n - viewW + 1));

    // ---- draw ---------------------------------------------------------------
    cb.fill(r.x, r.y, w, 1, ' ', TEXT, BG_SUNKEN);
    if (focused) cb.put(r.x, r.y, Gly::LEFT_HALF, BORDER_FOCUS, BG_SUNKEN);
    else if (ctx.isHot(id) && !dis) cb.put(r.x, r.y, Gly::LEFT_HALF, BORDER_DIM, BG_SUNKEN);

    if (n == 0 && placeholder && *placeholder) {
        cb.put(tx, r.y, Gly::SHADE_50, TEXT_MUTED, BG_SUNKEN);
        int c = 0;
        for (uint32_t cp : Utf8Decode(placeholder)) {
            if (++c >= viewW) break;
            cb.put(tx + c, r.y, cp, TEXT_MUTED, BG_SUNKEN);
        }
    } else {
        const uint8_t ink = dis ? (uint8_t)TEXT_MUTED : err ? (uint8_t)DANGER : (uint8_t)TEXT;
        const int a = edit::selMin(st), b = edit::selMax(st);
        for (int c = 0; c < viewW; ++c) {
            const int i = st.scroll_x + c;
            if (i >= n) break;
            const bool sel = focused && i >= a && i < b;
            cb.put(tx + c, r.y, t[(size_t)i], sel ? (uint8_t)TEXT_INV : ink,
                   sel ? (uint8_t)BG_SEL : (uint8_t)BG_SUNKEN);
        }
    }
    if (focused && edit::cursorVisible(ctx, st)) {
        const int c = st.cursor - st.scroll_x;
        if (c >= 0 && c < viewW) edit::drawCursor(cb, tx + c, r.y, t, st.cursor, st.overwrite);
    }
    if (err) cb.put(r.x + w - 2, r.y, Gly::CROSS_X, DANGER, BG_SUNKEN, ATTR_BOLD);
    Glitch(ctx, id, r, err && !dis);            // jolts once as the field goes into error
    return result;
}

} // namespace ui
