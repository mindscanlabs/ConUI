#pragma once
#include "../Context.hpp"

class CellBuffer;

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The editing core shared by TextInput (§8.12) and TextArea (§8.13). Internal
// to the widgets — nothing outside UI/Widgets/ includes this.
//
// The text is edited as CODEPOINTS, not bytes: one codepoint is one cell on
// the grid, so a cursor index is also a column, and Backspace after "ё"
// removes the letter rather than half of it. The caller's std::string stays
// UTF-8; it is decoded on the way in and encoded only when something changed.
//
// Wide (CJK) characters and combining marks are out of scope — the grid is
// one codepoint per cell, and IME is not supported.
// ---------------------------------------------------------------------------

namespace ui::edit {

using Text = std::vector<uint32_t>;

/** What a text widget remembers between frames (Context::blob). */
struct State {
    int    cursor = 0;
    int    anchor = 0;          // == cursor when nothing is selected
    int    scroll_x = 0;        // first visible column
    int    scroll_y = 0;        // first visible line (TextArea)
    int    want_col = -1;       // column Up/Down tries to keep, -1 = current
    bool   overwrite = false;   // Insert toggles it (§8.12: ▁ cursor)
    int    history = -1;        // TextInput: -1 = the draft, 0 = newest entry
    std::string draft;          // what was typed before walking the history
    double moved_at = 0.0;      // the cursor stays solid right after it moves
};

inline bool hasSelection(const State& s) { return s.cursor != s.anchor; }
inline int  selMin(const State& s) { return s.cursor < s.anchor ? s.cursor : s.anchor; }
inline int  selMax(const State& s) { return s.cursor < s.anchor ? s.anchor : s.cursor; }

std::string encode(const Text& t, int from, int to);
inline std::string encode(const Text& t) { return encode(t, 0, (int)t.size()); }

/** Line/column of a position, and back. Columns are clamped to the line. */
void lineCol(const Text& t, int pos, int& line, int& col);
int  posAt(const Text& t, int line, int col);
int  lineCount(const Text& t);
int  lineEnd(const Text& t, int pos);

/** Selects the word at `pos` (double-click). */
void selectWord(const Text& t, State& s, int pos);

struct Result {
    bool changed = false;   // the text changed
    bool moved = false;     // the cursor or selection moved
};

/**
 * Applies this frame's keyboard input to the text: typing, Backspace/Delete,
 * the arrows (Ctrl = by word, Shift = extend), Home/End, Insert, Ctrl+A/C/X/V.
 * Multi-line adds Enter, Up/Down, PgUp/PgDn (by `pageRows`) and
 * Ctrl+Home/End. Copy and cut go to Context::setClipboard().
 */
Result handleKeys(Context& ctx, Text& t, State& s, bool multiline, int pageRows);

/**
 * The pointer on a field: a press places the cursor (Shift extends the
 * selection), a double-click selects a word, and a drag after a single press
 * extends the selection. `pointerPos()` maps the pointer to a position in `t`.
 * Returns true when the cursor moved.
 */
template <class PointerPos>
bool handleMouse(Context& ctx, Id id, const Text& t, State& s, PointerPos pointerPos) {
    const Input& in = ctx.input();
    float& wordDrag = ctx.state(id);          // 1 after a double-click press
    if (ctx.justPressed(id)) {
        const int p = pointerPos();
        if (in.mouse_double) { selectWord(t, s, p); wordDrag = 1.0f; }
        else {
            s.cursor = p;
            if (!in.key_shift) s.anchor = p;
            wordDrag = 0.0f;
        }
        s.want_col = -1;
        s.moved_at = in.time;
        return true;
    }
    if (ctx.isActive(id) && in.mouse_down && wordDrag == 0.0f) {
        s.cursor = pointerPos();
        return true;
    }
    return false;
}

/** Solid for a moment after every move or keystroke, then the ~530 ms blink (§9). */
bool cursorVisible(const Context& ctx, const State& s);
/** Block cursor (inverts the letter under it), or ▁ in overwrite mode (§8.12). */
void drawCursor(CellBuffer& cb, int x, int y, const Text& t, int i, bool overwrite);

/** Clamps cursor/anchor after the caller's string changed under it. */
void clamp(const Text& t, State& s);

} // namespace ui::edit
