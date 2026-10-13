#include "TextEdit.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>

namespace ui::edit {

namespace {

/** 0 space, 1 word, 2 punctuation. Anything outside ASCII counts as a letter. */
int charClass(uint32_t c) {
    if (c == ' ' || c == '\t' || c == '\n') return 0;
    if (c >= 0x80) return 1;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
        return 1;
    return 2;
}

} // namespace

std::string encode(const Text& t, int from, int to) {
    std::string out;
    from = std::max(0, from);
    to = std::min((int)t.size(), to);
    for (int i = from; i < to; ++i) Utf8Append(out, t[(size_t)i]);
    return out;
}

void lineCol(const Text& t, int pos, int& line, int& col) {
    line = 0;
    int start = 0;
    pos = std::clamp(pos, 0, (int)t.size());
    for (int i = 0; i < pos; ++i)
        if (t[(size_t)i] == '\n') { ++line; start = i + 1; }
    col = pos - start;
}

int posAt(const Text& t, int line, int col) {
    // Walk to the start of `line`; a line past the last one is the last one.
    const int n = (int)t.size();
    int start = 0;
    for (int l = 0; l < line; ++l) {
        const int e = lineEnd(t, start);
        if (e >= n) break;
        start = e + 1;
    }
    return std::min(start + std::max(0, col), lineEnd(t, start));
}

int lineCount(const Text& t) {
    int n = 1;
    for (uint32_t c : t) if (c == '\n') ++n;
    return n;
}

namespace {

int lineStart(const Text& t, int pos) {
    pos = std::clamp(pos, 0, (int)t.size());
    while (pos > 0 && t[(size_t)pos - 1] != '\n') --pos;
    return pos;
}

} // namespace

int lineEnd(const Text& t, int pos) {
    const int n = (int)t.size();
    pos = std::clamp(pos, 0, n);
    while (pos < n && t[(size_t)pos] != '\n') ++pos;
    return pos;
}

namespace {

// Word boundaries for Ctrl+←/→ and Ctrl+Backspace/Delete.
int wordLeft(const Text& t, int pos) {
    pos = std::clamp(pos, 0, (int)t.size());
    while (pos > 0 && charClass(t[(size_t)pos - 1]) == 0) --pos;
    if (pos == 0) return 0;
    const int cls = charClass(t[(size_t)pos - 1]);
    while (pos > 0 && charClass(t[(size_t)pos - 1]) == cls) --pos;
    return pos;
}

int wordRight(const Text& t, int pos) {
    const int n = (int)t.size();
    pos = std::clamp(pos, 0, n);
    if (pos < n) {
        const int cls = charClass(t[(size_t)pos]);
        if (cls != 0) while (pos < n && charClass(t[(size_t)pos]) == cls) ++pos;
    }
    while (pos < n && charClass(t[(size_t)pos]) == 0 && t[(size_t)pos] != '\n') ++pos;
    return pos;
}

} // namespace

void selectWord(const Text& t, State& s, int pos) {
    const int n = (int)t.size();
    pos = std::clamp(pos, 0, n);
    // On the gap after the last letter, select the word before it.
    int p = (pos < n && charClass(t[(size_t)pos]) != 0) ? pos : pos - 1;
    if (p < 0 || p >= n || charClass(t[(size_t)p]) == 0) { s.cursor = s.anchor = pos; return; }
    const int cls = charClass(t[(size_t)p]);
    int a = p, b = p + 1;
    while (a > 0 && charClass(t[(size_t)a - 1]) == cls) --a;
    while (b < n && charClass(t[(size_t)b]) == cls) ++b;
    s.anchor = a;
    s.cursor = b;
}

namespace {

// Deletes the selection, if any. True if something was deleted.
bool eraseSelection(Text& t, State& s) {
    if (!hasSelection(s)) return false;
    const int a = selMin(s), b = selMax(s);
    t.erase(t.begin() + a, t.begin() + b);
    s.cursor = s.anchor = a;
    return true;
}

// Replaces the selection with `in` (control chars dropped; line breaks kept only if multiline).
void insert(Text& t, State& s, const uint32_t* in, int n, bool multiline) {
    eraseSelection(t, s);
    for (int i = 0; i < n; ++i) {
        uint32_t c = in[i];
        if (c == '\r') continue;                        // CRLF from the clipboard
        if (c == '\n' && !multiline) c = ' ';           // a pasted line break joins
        if (c == '\t') c = ' ';                         // no tab stops on the grid
        if ((c < 0x20 && c != '\n') || c == 0x7F) continue;
        // Overwrite replaces the character under the cursor — but never the
        // line break: typing at the end of a line extends it.
        if (s.overwrite && s.cursor < (int)t.size() && t[(size_t)s.cursor] != '\n' && c != '\n')
            t.erase(t.begin() + s.cursor);
        t.insert(t.begin() + s.cursor, c);
        ++s.cursor;
    }
    s.anchor = s.cursor;
}

} // namespace

bool cursorVisible(const Context& ctx, const State& s) {
    // A cursor that blinks off just as you type is hard to find.
    // Solid for 0.53 s after a move, then the blink; both need the frame that
    // ends them (blinkOn asks for its own).
    const double since = ctx.input().time - s.moved_at;
    if (since < 0.53) { ctx.animate(0.53 - since); return true; }
    return ctx.blinkOn();
}

void drawCursor(CellBuffer& cb, int x, int y, const Text& t, int i, bool overwrite) {
    const bool onChar = i < (int)t.size() && t[(size_t)i] != '\n';
    const uint32_t ch = onChar ? t[(size_t)i] : (uint32_t)' ';
    if (!cb.writable(x, y)) return;
    const uint8_t bg = cb.at(x, y).bg;
    if (overwrite) {
        // ▁ — the replace-mode cursor: under the character the next
        // keystroke will replace.
        if (onChar) cb.put(x, y, ch, ACCENT, bg, ATTR_UNDER | ATTR_BOLD);
        else        cb.put(x, y, Gly::LowerEighths(1), ACCENT, bg);
    } else if (onChar) {
        cb.put(x, y, ch, TEXT_INV, ACCENT);        // a block over a letter inverts it
    } else {
        cb.put(x, y, Gly::FULL, ACCENT, bg);
    }
}

void clamp(const Text& t, State& s) {
    const int n = (int)t.size();
    s.cursor = std::clamp(s.cursor, 0, n);
    s.anchor = std::clamp(s.anchor, 0, n);
}

Result handleKeys(Context& ctx, Text& t, State& s, bool multiline, int pageRows) {
    Result r;
    const Input& in = ctx.input();
    const bool sh = in.key_shift, ctrl = in.key_ctrl;
    clamp(t, s);

    auto moveTo = [&](int pos, bool keepCol) {
        s.cursor = std::clamp(pos, 0, (int)t.size());
        if (!sh) s.anchor = s.cursor;
        if (!keepCol) s.want_col = -1;
        r.moved = true;
    };

    // ---- the chords ------------------------------------------------------
    if (in.key_select_all) {
        s.anchor = 0;
        s.cursor = (int)t.size();
        r.moved = true;
    }
    if ((in.key_copy || in.key_cut) && hasSelection(s)) {
        ctx.setClipboard(encode(t, selMin(s), selMax(s)));
        if (in.key_cut) { eraseSelection(t, s); r.changed = true; }
    }
    if (in.key_paste && !in.paste.empty()) {
        const std::vector<uint32_t> cps = Utf8Decode(in.paste.c_str());
        insert(t, s, cps.data(), (int)cps.size(), multiline);
        r.changed = true;
    }

    // ---- typing ----------------------------------------------------------
    // Ctrl is not a filter here on purpose: AltGr arrives as Ctrl+Alt on
    // Windows, and "@" on a German keyboard must still type. Control
    // characters are dropped by insert() instead.
    if (!in.text.empty()) {
        insert(t, s, in.text.data(), (int)in.text.size(), multiline);
        r.changed = true;
    }
    if (multiline && in.key_enter) {
        const uint32_t nl = '\n';
        insert(t, s, &nl, 1, true);
        r.changed = true;
    }

    // ---- deleting --------------------------------------------------------
    if (in.key_backspace) {
        if (!eraseSelection(t, s) && s.cursor > 0) {
            const int from = ctrl ? wordLeft(t, s.cursor) : s.cursor - 1;
            t.erase(t.begin() + from, t.begin() + s.cursor);
            s.cursor = s.anchor = from;
        }
        r.changed = true;
    }
    if (in.key_delete) {
        if (!eraseSelection(t, s) && s.cursor < (int)t.size()) {
            const int to = ctrl ? wordRight(t, s.cursor) : s.cursor + 1;
            t.erase(t.begin() + s.cursor, t.begin() + std::max(to, s.cursor + 1));
        }
        s.anchor = s.cursor;
        r.changed = true;
    }
    if (in.key_insert) s.overwrite = !s.overwrite;

    // ---- moving ----------------------------------------------------------
    if (in.key_left) {
        if (hasSelection(s) && !sh) moveTo(selMin(s), false);
        else moveTo(ctrl ? wordLeft(t, s.cursor) : s.cursor - 1, false);
    }
    if (in.key_right) {
        if (hasSelection(s) && !sh) moveTo(selMax(s), false);
        else moveTo(ctrl ? wordRight(t, s.cursor) : s.cursor + 1, false);
    }
    if (in.key_home) moveTo(multiline && ctrl ? 0 : lineStart(t, s.cursor), false);
    if (in.key_end)  moveTo(multiline && ctrl ? (int)t.size() : lineEnd(t, s.cursor), false);

    if (multiline) {
        int dl = 0;
        if (in.key_up)   dl -= 1;
        if (in.key_down) dl += 1;
        if (in.key_pgup) dl -= std::max(1, pageRows - 1);
        if (in.key_pgdn) dl += std::max(1, pageRows - 1);
        if (dl != 0) {
            // Up/Down keep the column the cursor STARTED in, across short
            // lines — the way every editor does it and the way hands expect.
            int line = 0, col = 0;
            lineCol(t, s.cursor, line, col);
            if (s.want_col < 0) s.want_col = col;
            const int last = lineCount(t) - 1;
            const int target = std::clamp(line + dl, 0, last);
            if (target != line) moveTo(posAt(t, target, s.want_col), true);
            else if (dl < 0) moveTo(lineStart(t, s.cursor), false);     // top line: go home
            else moveTo(lineEnd(t, s.cursor), false);                    // bottom: go end
        }
    }

    if (r.changed) s.want_col = -1;
    if (r.changed || r.moved) s.moved_at = in.time;
    return r;
}

} // namespace ui::edit
