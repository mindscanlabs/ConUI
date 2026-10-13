#include "ThemeEditor.hpp"
#include "../Core/ThemeStore.hpp"
#include "../Render/CellBuffer.hpp"
#include "../UI/Context.hpp"
#include "../UI/Glyphs.hpp"
#include "../UI/Layout.hpp"
#include "../UI/Strings.hpp"
#include "../UI/Theme.hpp"
#include "../UI/Widgets/Widgets.hpp"
#include "../UI/WindowChrome.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// The layout (§8.16):
//
//   [ NEW ] ◆ SAVE ◆ [ REVERT ]
//  ───────────────────────────────────────────────────────────
//   THEME ▌ phosphor                ▼▌
//   FILE  themes/phosphor.json                     ● modified
//  ───────────────────────────────────────────────────────────
//   SLOT               HEX  ▮ ┊ [ bg.panel ]
//   ██ bg.window   #062038    ┊ ██████████   #0B3055
//  ▐██ bg.panel    #0B3055 ▌  ┊ R ├■──────────┤   11
//   …                         ┊ …  HEX ▌#0B3055█  ▌
//                             ┊ ─ LIVE PREVIEW ────────
//  ───────────────────────────────────────────────────────────
//   20 SLOTS  3 changed               Ctrl+S save · Esc close

namespace {

using namespace ui;

/** A path cut from the LEFT with "…" — the file name is the part worth keeping. */
std::string tailFit(const std::string& s, int w) {
    std::vector<uint32_t> cps = Utf8Decode(s.c_str());
    if ((int)cps.size() <= w || w <= 1) return s;
    std::string out = "…";
    for (size_t i = cps.size() - (size_t)(w - 1); i < cps.size(); ++i) Utf8Append(out, cps[i]);
    return out;
}

/** The slot list: a swatch that paints itself with its own slot, the name, the hex. */
void slotList(Context& ctx, ThemeEditorState& st, const Theme& t, const Rect& r) {
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    if (r.w < 12 || r.h < 3) return;

    const int x0 = r.x + 1;
    const int right = r.x + r.w - 2;                    // last column before the bar
    cb.text(x0, r.y, str::T(str::ID_ED_SLOT), TEXT_MUTED, BG_PANEL);
    cb.textRight(right - 1, r.y, "HEX", TEXT_MUTED, BG_PANEL);

    const Rect rows{ r.x, r.y + 1, r.w - 1, r.h - 1 };
    const Id id = ctx.makeId("slots");
    ctx.focusable(id);
    ctx.interact(id, rows);
    const bool focused = ctx.isFocused(id);

    bool follow = false;
    if (ctx.justPressed(id)) {
        const int s = st.list_scroll + (int)std::floor(in.mouse_y - (float)rows.y);
        if (s >= 0 && s < SLOT_COUNT) st.slot = s;
    }
    if (focused) {
        const int before = st.slot;
        if (in.key_up)   st.slot = std::max(0, st.slot - 1);
        if (in.key_down) st.slot = std::min(SLOT_COUNT - 1, st.slot + 1);
        if (in.key_home) st.slot = 0;
        if (in.key_end)  st.slot = SLOT_COUNT - 1;
        follow = st.slot != before;
    }
    if (rows.contains(in.mouse_x, in.mouse_y)) {
        const float w = ctx.takeWheel();
        if (w != 0.0f) st.list_scroll -= (int)std::lround(w * 3.0f);
    }
    if (follow) {
        if (st.slot < st.list_scroll) st.list_scroll = st.slot;
        if (st.slot >= st.list_scroll + rows.h) st.list_scroll = st.slot - rows.h + 1;
    }
    st.list_scroll = std::clamp(st.list_scroll, 0, std::max(0, SLOT_COUNT - rows.h));

    ctx.pushRegion(rows);
    for (int k = 0; k < rows.h; ++k) {
        const int s = st.list_scroll + k;
        if (s >= SLOT_COUNT) break;
        const int y = rows.y + k;
        const bool sel = s == st.slot;
        const uint8_t bg = sel ? (uint8_t)BG_SEL : (uint8_t)BG_PANEL;
        if (sel) {
            cb.put(r.x, y, Gly::RIGHT_HALF, BG_SEL, BG_PANEL);
            cb.bgRect(x0, y, right - x0, 1, BG_SEL);
            cb.put(right, y, Gly::LEFT_HALF, BG_SEL, BG_PANEL);
        }
        // The swatch is just a glyph in its own slot's colour (§8.16) — no
        // separate "show a colour" mechanism exists or is needed.
        cb.put(x0, y, Gly::FULL, (uint8_t)s, bg);
        cb.put(x0 + 1, y, Gly::FULL, (uint8_t)s, bg);
        // The value matters more than the full name: the hex is always shown,
        // and a name that would run into it is cut with "…".
        const std::string hex = FormatHexColor(t.c[s]);
        const int nameRoom = std::max(1, (right - 1 - (int)hex.size()) - (x0 + 3));
        std::vector<uint32_t> name = Utf8Decode(kSlotNames[s]);
        if ((int)name.size() > nameRoom) { name.resize((size_t)nameRoom); name.back() = 0x2026; }
        int nx = x0 + 3;
        for (uint32_t cp : name)
            cb.put(nx++, y, cp, sel ? (uint8_t)TEXT_INV : (uint8_t)TEXT, bg, sel ? ATTR_BOLD : ATTR_NONE);
        cb.textRight(right - 1, y, hex.c_str(), sel ? (uint8_t)TEXT_INV : (uint8_t)TEXT_MUTED, bg);
    }
    ctx.popRegion();
    ScrollbarV(ctx, "slotbar", r.x + r.w - 1, rows.y, rows.h, SLOT_COUNT, rows.h, st.list_scroll,
               ScrollLive(ctx, r, focused));
}

/** The editor of one slot, and the preview under it. */
void slotEditor(Context& ctx, ThemeEditorState& st, Theme& t, const Rect& r) {
    CellBuffer& cb = ctx.cells();
    if (r.w < 20 || r.h < 8) return;
    Rgba& col = t.c[st.slot];
    ctx.pushRegion(r);

    // [ bg.panel ]
    {
        const Rect h = ctx.allocateRow(1);
        int x = h.x;
        cb.put(x++, h.y, '[', BORDER, BG_PANEL);
        cb.put(x++, h.y, ' ', BORDER, BG_PANEL);
        x += cb.text(x, h.y, kSlotNames[st.slot], ACCENT, BG_PANEL, ATTR_BOLD);
        cb.put(x++, h.y, ' ', BORDER, BG_PANEL);
        cb.put(x, h.y, ']', BORDER, BG_PANEL);
        ctx.newline(2);
    }
    // the big swatch
    {
        const Rect s = ctx.allocate(10, 1);
        for (int i = 0; i < 10; ++i) cb.put(s.x + i, s.y, Gly::FULL, (uint8_t)st.slot, BG_PANEL);
        ctx.spacer(3);
        Label(ctx, FormatHexColor(col).c_str(), TEXT, ATTR_BOLD);
        ctx.newline(2);
    }
    // R / G / B / A — the ordinary slider (§8.4), nothing new
    const int sw = std::max(6, ctx.regionWidth() - 3 - 5);
    uint8_t* ch[4] = { &col.r, &col.g, &col.b, &col.a };
    static const char* kCh[4] = { "R", "G", "B", "A" };
    for (int i = 0; i < 4; ++i) {
        LabelFixed(ctx, kCh[i], 3, TEXT2);
        float v = (float)*ch[i];
        if (Slider(ctx, kCh[i], sw, v, 0.0f, 255.0f, 1.0f)) *ch[i] = (uint8_t)std::lround(v);
        char b[8];
        std::snprintf(b, sizeof b, "%d", (int)*ch[i]);
        LabelRight(ctx, b, TEXT, ATTR_BOLD);
        ctx.newline();
    }
    ctx.newline();

    // HEX — a TextInput with validation (§8.16): a wrong string turns red and
    // is NOT applied, rather than resetting the colour under the user's hands.
    {
        LabelFixed(ctx, "HEX", 5, TEXT2);
        const Id hexId = ctx.makeId("hex");
        if (ctx.focusedId() != hexId) st.hex = FormatHexColor(col);   // follow the sliders
        Rgba parsed;
        const bool valid = ParseHexColor(st.hex.c_str(), parsed);
        const int res = TextInput(ctx, "hex", std::min(14, ctx.regionWidth() - 5), st.hex,
                                  nullptr, valid ? TXT_NONE : TXT_ERROR);
        if ((res & TXT_CHANGED) && ParseHexColor(st.hex.c_str(), parsed)) col = parsed;
        ctx.newline(2);
    }

    // ─ LIVE PREVIEW ───── drawn with the palette being edited
    {
        const Rect h = ctx.allocateRow(1);
        cb.put(h.x, h.y, Gly::H, BORDER_DIM, BG_PANEL);
        const int tx = h.x + 2 + cb.text(h.x + 2, h.y, str::T(str::ID_ED_PREVIEW), TEXT2, BG_PANEL);
        for (int x = tx + 1; x < h.x + h.w; ++x) cb.put(x, h.y, Gly::H, BORDER_DIM, BG_PANEL);
        ctx.newline();
    }
    const int left = ctx.region().y + ctx.region().h - ctx.cursorY();
    if (left >= 7) {
        // One more row for the retro bar (retro.filled / retro.empty) when
        // there is room for it.
        const bool retro = left >= 8;
        const Rect pr{ ctx.cursorX(), ctx.cursorY(), ctx.regionWidth(), retro ? 8 : 7 };
        BeginPanel(ctx, "preview", "PANEL", pr);
        Label(ctx, "text.primary", TEXT);
        ctx.newline();
        const Rect sel = ctx.allocateRow(1);
        cb.put(sel.x - 1, sel.y, Gly::RIGHT_HALF, BG_SEL, BG_PANEL);
        cb.bgRect(sel.x, sel.y, sel.w, 1, BG_SEL);
        cb.put(sel.x + sel.w, sel.y, Gly::LEFT_HALF, BG_SEL, BG_PANEL);
        cb.text(sel.x, sel.y, str::T(str::ID_ED_PV_SEL), TEXT_INV, BG_SEL, ATTR_BOLD);
        ctx.newline();
        Button(ctx, "BTN");
        ctx.spacer(1);
        const int pw = std::max(4, ctx.regionWidth() - 9 - 5);
        ProgressBar(ctx, pw, 0.62f);
        LabelRight(ctx, "62%", TEXT, ATTR_BOLD);
        ctx.newline();
        if (retro) {
            ProgressRetro(ctx, std::max(4, ctx.regionWidth() - 6), 5.0f / 8.0f);
            LabelRight(ctx, "5/8", TEXT, ATTR_BOLD);
            ctx.newline();
        }
        Label(ctx, "░▒▓", DITHER);
        ctx.spacer(1);
        Label(ctx, str::T(str::ID_ED_PV_DITHER), TEXT2);
        ctx.spacer(3);
        Label(ctx, "✗", DANGER, ATTR_BOLD);
        ctx.spacer(1);
        Label(ctx, str::T(str::ID_ED_PV_DANGER), DANGER);
        EndPanel(ctx);
    }
    ctx.popRegion();
}

} // namespace

EditorResult BuildThemeEditor(Context& ctx, Layout& lay, ThemeEditorState& st,
                              ThemeStore& themes, int themeIndex, int top) {
    EditorResult res;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    const int W = cb.width(), H = cb.height();
    if (W < 44 || H < top + 16) {
        if (H > top) cb.text(1, top, "window too small", TEXT_MUTED, BG_WINDOW);
        return res;
    }
    Theme& t = themes.edit(themeIndex);
    st.slot = std::clamp(st.slot, 0, SLOT_COUNT - 1);
    ctx.pushId("theme-editor");

    // ---- toolbar ------------------------------------------------------------------
    std::string err;
    ctx.pushRegion(Rect{ 2, top, W - 4, 1 });
    if (Button(ctx, str::ID_ED_NEW)) res.select_theme = themes.duplicate(themeIndex);
    ctx.spacer(1);
    const bool save = Button(ctx, str::ID_SAVE, BTN_DEFAULT) || in.key_save;
    ctx.spacer(1);
    ctx.beginDisabled(!themes.modified(themeIndex));
    if (Button(ctx, str::ID_ED_REVERT)) { themes.revert(themeIndex); st.message.clear(); }
    ctx.endDisabled();
    ctx.popRegion();
    if (save) {
        st.message_error = !themes.saveAt(themeIndex, err);
        st.message = st.message_error ? err : std::string(str::T(str::ID_ED_SAVED));
    }
    chrome::DrawRule(cb, top + 1);

    // ---- theme and file -----------------------------------------------------------
    std::vector<const char*> names;
    for (int i = 0; i < themes.count(); ++i) names.push_back(themes.at(i).name.c_str());
    ctx.pushRegion(Rect{ 2, top + 2, W - 4, 2 });
    LabelFixed(ctx, str::T(str::ID_MENU_THEME), 7);
    int pick = themeIndex;
    if (Dropdown(ctx, "theme", std::min(W - 13, 30), names.data(), (int)names.size(), pick))
        res.select_theme = pick;
    ctx.newline();
    LabelFixed(ctx, str::T(str::ID_ED_FILE), 7);
    const bool mod = themes.modified(themeIndex);
    const char* flag = str::T(mod ? str::ID_ED_MODIFIED : str::ID_ED_SAVED);
    const int room = W - 4 - 7 - Utf8Width(flag) - 2;
    Label(ctx, tailFit(themes.pathOf(themeIndex), room).c_str(), TEXT_MUTED);
    LabelRight(ctx, flag, mod ? (uint8_t)WARN : (uint8_t)TEXT_MUTED, mod ? ATTR_BOLD : ATTR_NONE);
    ctx.popRegion();
    chrome::DrawRule(cb, top + 4);

    // ---- body: slot list │ slot editor, a draggable seam between (§8.17) -------
    const Rect body{ 1, top + 5, W - 2, H - 3 - (top + 5) + 1 };
    Rect left, right;
    Split(ctx, lay, "editor", Axis::Cols, body, 0.52f, 24, 26, left, right);
    slotList(ctx, st, t, left);
    slotEditor(ctx, st, t, Rect{ right.x + 1, right.y, right.w - 1, right.h });

    // ---- status ---------------------------------------------------------------------
    chrome::DrawRule(cb, H - 2, /*raised=*/true);
    {
        char a[32], b[32];
        std::snprintf(a, sizeof a, str::T(str::ID_ED_SLOTS), SLOT_COUNT);
        std::snprintf(b, sizeof b, str::T(str::ID_ED_CHANGED), themes.changedSlots(themeIndex));
        int x = 2;
        x += cb.text(x, H - 1, a, TEXT2, BG_WINDOW, ATTR_BOLD) + 2;
        x += cb.text(x, H - 1, b, mod ? (uint8_t)WARN : (uint8_t)TEXT_MUTED, BG_WINDOW) + 2;
        const char* hint = str::T(str::ID_ED_HINT);
        const int hw = Utf8Width(hint);
        if (!st.message.empty() && x + Utf8Width(st.message.c_str()) + hw + 4 < W)
            cb.text(x, H - 1, st.message.c_str(), st.message_error ? (uint8_t)DANGER : (uint8_t)SUCCESS,
                    BG_WINDOW);
        if (x + hw + 2 < W) cb.textRight(W - 3, H - 1, hint, TEXT_MUTED, BG_WINDOW);
    }

    ctx.popId();
    // Esc closes the editor — unless a text field or the dropdown kept it.
    if (in.key_escape && !ctx.escapeClaimed()) res.close = true;
    return res;
}
