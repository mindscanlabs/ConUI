#pragma once
#include "../Context.hpp"
#include "../Strings.hpp"
#include "../Theme.hpp"

#include <string>
#include <vector>

class CellBuffer;

// ---------------------------------------------------------------------------
// The widget set (docs/DESIGN_SYSTEM.md §8).
//
// Every widget is a free function over a Context: it allocates its cells at
// the layout cursor, routes input through Context::interact(), draws, and
// returns what happened. The caller owns the VALUE (a bool, a float, an int);
// the context owns only what the caller has no business keeping — hover,
// focus, where a drag grabbed.
//
// Shared visual rules, so they are stated once rather than in every file:
//
//   * Keyboard focus = the widget's edge glyphs in `border.focus`, and a ◆
//     blinking in place of its FIRST glyph (§9). One rule for all widgets:
//     the diamond never needs a cell of its own, so focusing a widget never
//     shifts the layout.
//   * Hover = the bracket swap or the accent edge; press = inversion (§9).
//   * Disabled = `text.muted`, no input, out of the Tab order.
//   * Widgets write BG_PANEL as their cell background. It is a LAYER colour
//     (§2): the cell paints nothing and whatever surface is underneath —
//     panel or bare window — shows through. So the same widget is correct on
//     either, without being told which.
// ---------------------------------------------------------------------------

namespace ui {

// ---- Panel (§8.1) --------------------------------------------

enum PanelFlags : int {
    PANEL_NONE  = 0,
    /** Content runs from frame to frame: lists whose selection is full-width. */
    PANEL_FLUSH = 1 << 0,
    /** Frame in `border.focus` — the panel holds the keyboard focus. */
    PANEL_FOCUS = 1 << 1,
};

/**
 * ┌─[ TITLE ]──────── status ─┐ and its surface. Pushes an id scope and a
 * region for the inside; returns that region. Pair with EndPanel().
 *
 * `id` scopes the widgets inside and is separate from `title` on purpose:
 * the title is localised, and a scope that changed with the language would
 * forget every scroll position and drag the moment the locale switched.
 *
 * The inside leaves the first and last columns free: the left one is a
 * margin, the right one is where a scrollbar goes (§8.14) — so a panel
 * that gains a scrollbar does not reflow.
 */
Rect BeginPanel(Context& ctx, const char* id, const char* title, const Rect& r,
                const char* status = nullptr, int flags = PANEL_NONE);
void EndPanel(Context& ctx);

/** The bare frame and its ground, cut to the frame line — for things that are not panels (a popup, a box). */
void DrawFrame(CellBuffer& cb, const Rect& r, uint8_t fg, uint8_t bg);
/** ─[ TITLE ]─ sunk into a top border at (x, y). */
void DrawFrameTitle(CellBuffer& cb, int x, int y, const char* title,
                    uint8_t fg, uint8_t bg, uint8_t titleFg);
/**
 * A panel's title in its top border — `accent` on `bg.panel` — when the frame
 * is wide enough for all of it; dropped, never cut, when it is not: half a
 * title reads as a different word. Returns the cells it took from the border
 * (0 when dropped), so the caller can fit something at the right end.
 */
int DrawPanelTitle(CellBuffer& cb, const Rect& r, const char* title, uint8_t edge,
                   uint8_t titleFg = ACCENT);

// ---- Text -------------------------------------------------------------------

/** A run of text at the cursor. */
void Label(Context& ctx, const char* s, uint8_t slot = TEXT, uint8_t attr = 0);
/** Text in a column `w` cells wide, so the next widget lines up (form labels). */
void LabelFixed(Context& ctx, const char* s, int w, uint8_t slot = TEXT2);
/** Text right-aligned against the region's right edge, on the current line. */
void LabelRight(Context& ctx, const char* s, uint8_t slot = TEXT, uint8_t attr = 0);

// ---- Button (§8.2) ------------------------------------------------------------

enum ButtonFlags : int {
    BTN_NONE    = 0,
    BTN_DANGER  = 1 << 0,   // irreversible: red text and brackets
    BTN_DEFAULT = 1 << 1,   // ◆ RUN ◆ — what Enter would do
};

/** [ LABEL ]. True on a completed click or Enter/Space while focused. */
bool Button(Context& ctx, const char* label, int flags = BTN_NONE);
bool Button(Context& ctx, str::Id label, int flags = BTN_NONE);

// ---- Toggles (§8.5) -----------------------------------------------------------

/** [✓] Label. True when the value changed. */
bool Checkbox(Context& ctx, const char* label, bool& v);
/** [ ] / [✓] / [~]: 0 off, 1 on, 2 mixed. Clicking mixed turns it on. */
bool CheckboxTri(Context& ctx, const char* label, int& v);
/** (●) Label. Sets `sel = value` when picked; true when that changed it. */
bool Radio(Context& ctx, const char* label, int& sel, int value);
/** A track like the progress bar's with a thumb over half of it: right and
 *  `success` when on, left and muted when off (§8.5). */
bool Switch(Context& ctx, const char* label, bool& v);

// ---- Progress (§8.3) ------------------------------------------------------------

/** The smooth bar in the shape layer: track `dither`, fill `slot`. */
void ProgressBar(Context& ctx, int w, float v, uint8_t slot = ACCENT);
/**
 * [■■■■□□□□□□] — all glyphs, for build-log texture. ■ in `slot`
 * (the theme's `retro.filled` by default), □ always in `retro.empty`.
 */
void ProgressRetro(Context& ctx, int w, float v, uint8_t slot = RETRO_ON);
/** Indeterminate: the same track with a travelling highlight (§9 animation). */
void ProgressBusy(Context& ctx, int w, uint8_t slot = ACCENT);

// ---- Slider (§8.4) --------------------------------------------------------------

/**
 * A line and a square knob, all geometry at the value's exact position (§8.4).
 * `step` 0 is continuous; above 0 the value snaps and the line shows ticks.
 * Drag, wheel, ←/→ (Shift = fine step). True when changed.
 */
bool Slider(Context& ctx, const char* id, int w, float& v,
            float lo = 0.0f, float hi = 1.0f, float step = 0.0f);
/** Two knobs, the span between them in `accent`. Keeps a <= b. */
bool SliderRange(Context& ctx, const char* id, int w, float& a, float& b,
                 float lo = 0.0f, float hi = 1.0f, float step = 0.0f);

// ---- Scrolling (§8.14) -------------------------------------------------------------

/**
 * A vertical thumb in the shape layer: no arrows, no track (§8.14). Drawn
 * only when there is something to scroll. Drag the thumb, or click above or
 * below it for a page. True when `offset` changed.
 *
 * @param x,y,h   the one cell column it lives in
 * @param total   content length, rows
 * @param visible rows on screen
 * @param live    the region it scrolls is under the pointer or holds the
 *                focus; otherwise the bar is not drawn at all. A thumb
 *                being dragged stays whatever this says. `ScrollLive()`
 *                answers it for a plain rectangle.
 */
bool ScrollbarV(Context& ctx, const char* id, int x, int y, int h,
                int total, int visible, int& offset, bool live = true);
/**
 * The same object turned sideways, the same thickness in PIXELS. It sits flush
 * against the bottom of its row — a frame's border is right under it — or,
 * `centred`, in the middle of the row, where there is no border to rest on.
 */
bool ScrollbarH(Context& ctx, const char* id, int x, int y, int w,
                int total, int visible, int& offset, bool live = true,
                bool centred = false);
/** `focused`, or the pointer over `r` and no overlay in between. */
bool ScrollLive(Context& ctx, const Rect& r, bool focused);

/**
 * Everything between BeginScroll and EndScroll is laid out as if `r` were
 * infinitely tall, then clipped to it. The rightmost column of `r` is the
 * scrollbar's. Wheel over the region, PageUp/PageDown/Home/End while the
 * pointer or the focus is inside, and Tab into a widget below the fold
 * scrolls it into view.
 *
 * `padX`/`padY` inset the content from the clip — a panel's margin — while
 * the clip itself still reaches the frame, so rows scroll right up to it.
 */
void BeginScroll(Context& ctx, const char* id, const Rect& r, int padX = 0, int padY = 0);
void EndScroll(Context& ctx);
/** Scroll offset of a region, in rows — for a status line, and for tests. */
int  ScrollOffset(Context& ctx, const char* id);

// ---- Text (§8.12, §8.13) -------------------------------------------------------

enum TextFlags : int {
    TXT_NONE  = 0,
    TXT_ERROR = 1 << 0,     // text in `danger` and a ✗ at the right end
};
enum TextResult : int {
    TXT_CHANGED   = 1 << 0,
    TXT_SUBMITTED = 1 << 1, // Enter in a single-line field
};

/**
 * The single-line field. Editing is by codepoint (one codepoint = one cell):
 * typing, Backspace/Delete (Ctrl = by word), ←/→ (Ctrl = by word, Shift =
 * select), Home/End, Insert (overwrite, ▁ cursor), Ctrl+A/C/X/V, click,
 * drag-select, double-click selects a word.
 *
 * `history`, if given, is the caller's list: Enter appends to it, ↑/↓ walk
 * it, and ↓ past the newest entry returns what was being typed.
 *
 * @return TextResult bits.
 */
int TextInput(Context& ctx, const char* id, int w, std::string& s,
              const char* placeholder = nullptr, int flags = TXT_NONE,
              std::vector<std::string>* history = nullptr);

/**
 * The multi-line editor in its own frame: line numbers in a gutter, `~` past
 * the end, a status bar with line/column, a scrollbar. Everything TextInput
 * does, plus Enter, ↑/↓ (keeping the column), PgUp/PgDn, Ctrl+Home/End. The
 * wheel scrolls the view without moving the cursor.
 *
 * Tab still moves focus: a text area that swallowed Tab would be a keyboard
 * trap, and the grid has no tab stops to insert anyway.
 */
int TextArea(Context& ctx, const char* id, const char* title, const Rect& r, std::string& s);

// ---- Table (§8.8) -------------------------------------------------------------------

struct TableColumn {
    const char* title;
    int  width;            // content cells, without the padding
    bool right = false;    // numbers go right (§8.8)
};

enum TableEvent : int {
    TBL_SELECT   = 1 << 0,  // `selected` changed
    TBL_SORT     = 1 << 1,  // a header was clicked: re-sort by `sortCol`/`sortDesc`
    TBL_ACTIVATE = 1 << 2,  // double-click or Enter on the selected row
};

enum TableFlags : int {
    TBL_FRAMED = 0,
    TBL_BARE   = 1 << 0,    // no frame, no title, no row count: the header is the top row (§8.8)
};

/**
 * A framed table: title and row count in the top border, a raised header
 * with the sort arrow, │ between columns, full-width inverted selection,
 * hover in `bg.raised`, a vertical scrollbar, and a horizontal one when the
 * columns are wider than the frame. The last column stretches.
 *
 * The widget never sorts: it reports the click, the caller owns the data and
 * the order. Rows are indices into the caller's (already sorted) data.
 * ↑/↓/PgUp/PgDn/Home/End move the selection and keep it in view.
 *
 * TBL_BARE drops the frame with its title and row count: the header sits on
 * r.y, a rule under it, the rows below, the scrollbar in the last column; the
 * bottom row is the table's margin, and holds the horizontal scrollbar when
 * the columns overflow.
 */
int Table(Context& ctx, const char* id, const char* title, const Rect& r,
          const TableColumn* cols, int ncols, int rows,
          const char* (*cell)(void* user, int row, int col), void* user,
          int& selected, int& sortCol, bool& sortDesc, int flags = TBL_FRAMED);

// ---- Editable table (§8.19) ----------------------------------------------------------

// The same columns as the read-only table: title, width, alignment.
using EditColumn = TableColumn;

enum EditTableEvent : int {
    ET_EDITED = 1 << 0,    // a cell's text was committed: row `evRow`
    ET_SORT   = 1 << 1,    // a header was clicked: re-sort by `sortCol`/`sortDesc`
    ET_INSERT = 1 << 2,    // insert an empty row AT `evRow`
    ET_DELETE = 1 << 3,    // delete row `evRow`
};

/**
 * A framed table whose cells are edited in place, with multi-line text: a row
 * is as tall as its tallest cell (up to 6 lines; more shows …), rows are
 * divided by a one-pixel line.
 *
 * Not editing: ←/→/↑/↓ move the cell, Home/End the column, Ctrl+Home/End the
 * row, PgUp/PgDn a page; Enter, F2 or a double-click edit the cell keeping
 * its text; typing a character edits it replacing the text; Delete clears it;
 * Ctrl+Insert / Ctrl+Delete — and the right-click menu — insert or delete a
 * row. Editing: Enter commits, Shift+Enter is a new line, Tab / Shift+Tab
 * commit and step to the next / previous cell, Esc cancels; a click on
 * another cell or leaving the table commits.
 *
 * The caller owns the data and the order: `cell` returns a pointer to the
 * caller's string (null = empty and read-only). The widget writes a committed
 * edit into it, but never sorts, inserts or deletes — it reports ET_SORT,
 * ET_INSERT, ET_DELETE with the row in `evRow`, like Table.
 *
 * @return EditTableEvent bits.
 */
int EditTable(Context& ctx, const char* id, const char* title, const Rect& r,
              const EditColumn* cols, int ncols, int rows,
              std::string* (*cell)(void* user, int row, int col), void* user,
              int& selRow, int& selCol, int& sortCol, bool& sortDesc, int& evRow);

// ---- Charts (§8.20) -----------------------------------------------------------------

/** One series: its name (the legend, the tooltip), `n` values, its colour. */
struct ChartSeries {
    const char*  name;
    const float* values;
    uint8_t      slot = CHART_1;
};

enum ChartFlags : int {
    CHART_NONE       = 0,
    CHART_SMOOTH     = 1 << 0,  // LineChart: a smooth curve through the points, not straight runs
    CHART_GRID       = 1 << 1,  // dashed lines across the plot at the value ticks
    CHART_HORIZONTAL = 1 << 2,  // BarChart: bars grow to the right, categories down the side
};

/**
 * A framed line chart: the title and a legend (■ name per series) in the top
 * border, the value axis on the left with ┤ ticks on whole rows, the category
 * axis along the bottom with its labels (thinned out when they would
 * collide). Lines are vector segments (§2.1), straight or CHART_SMOOTH — a
 * monotone curve that never overshoots the data. Hovering shows a guide, a
 * dot on each series and a tooltip with the values.
 *
 * The value range is `vmin`..`vmax`; pass vmin >= vmax to fit it to the data
 * on "nice" numbers. A non-finite value is a gap in its line.
 */
void LineChart(Context& ctx, const char* id, const char* title, const Rect& r,
               const ChartSeries* series, int nseries, int n, const char* const* labels,
               float vmin = 0.0f, float vmax = 0.0f, int flags = CHART_GRID);

/**
 * A framed bar chart, grouped: one bar per series in each category, bars in
 * the shape layer like the progress bar (§2.1), rising from zero. Vertical by
 * default, CHART_HORIZONTAL lays the categories down the left side. The same
 * legend, grid and tooltip as LineChart. `vmax` <= 0 fits the range to the data.
 */
void BarChart(Context& ctx, const char* id, const char* title, const Rect& r,
              const ChartSeries* series, int nseries, int n, const char* const* labels,
              float vmax = 0.0f, int flags = CHART_GRID);

// ---- Tree (§8.7) --------------------------------------------------------------------

/** The caller's data. The tree widget only reads it and flips `open`. */
struct TreeNode {
    std::string label;
    std::string meta;                  // right-aligned, muted: a size, a count
    std::vector<TreeNode> children;
    bool open = false;
    bool dir = false;                  // a folder even when empty
    bool isDir() const { return dir || !children.empty(); }
};

enum TreeEvent : int {
    TREE_SELECT   = 1 << 0,
    TREE_TOGGLE   = 1 << 1,
    TREE_ACTIVATE = 1 << 2,            // Enter or double-click on a leaf
};

/**
 * ├── └── │ connectors, ▼/▶ to fold, full-width inverted selection
 *, a collapsed folder shows its child count. Draws inside `r` (use a
 * PANEL_FLUSH panel's inside); the rightmost column is the scrollbar's.
 *
 * Keys: ↑/↓ select, → opens a folder or steps into it, ← closes or steps out
 * to the parent, Enter/Space toggles a folder or activates a leaf.
 */
int Tree(Context& ctx, const char* id, const Rect& r,
         std::vector<TreeNode>& roots, const TreeNode*& selected);

// ---- Diagrams (§8.21) ---------------------------------------------------------------
//
// Pictures of structure: a tree of boxes and a layered network. Both draw
// inside `r` without a frame (use a panel's inside, or part of it under the
// panel's own controls) and share one view, which the context keeps under
// the widget's id: the wheel zooms about the cursor (1× to 16×), dragging
// moves the picture, a double click shows it whole again. Zoom spreads the
// picture apart; the letters keep their size.

enum DiagramFlags : int {
    DIAG_NONE = 0,
    DIAG_HINT = 1 << 0,     // "wheel: zoom · drag: move · double-click: reset" on the last row
};

/** One box of a TreeDiagram. The caller's data; the widget only reads it. */
struct DiagramNode {
    std::string label;
    int     parent = -1;     // an EARLIER node's index; anything else makes it a root
    uint8_t slot = TEXT;     // the box's outline and text
    bool    hidden = false;  // not drawn, nor its line up — but its place is kept,
                             // so a tree being assembled does not jump about
};

/**
 * A tree of boxes, root at the top: each label bold in a rounded box (the
 * outline in the node's slot, the ground `bg.raised`) on whole cells,
 * children in the next row down, at most 3 rows apart, joined to their
 * parent by lines from the bottom middle to the top middle. Leaves stand
 * side by side across the width, a parent over the middle of its children;
 * several roots stand side by side. Children are drawn in the order they
 * appear in `nodes`.
 *
 * @return the node under the pointer, or -1 — to show a Tooltip for it
 *         right after the call (its outline is drawn heavier).
 */
int TreeDiagram(Context& ctx, const char* id, const Rect& r, const DiagramNode* nodes, int n,
                int flags = DIAG_HINT);

/** One layer of a NetDiagram. */
struct NetLayer {
    int     n = 0;                          // nodes
    uint8_t slot = TEXT2;                   // their colour, and their names'
    const char* const* labels = nullptr;    // `n` names: drawn for the first layer (left
                                            // of it) and the last (right of it) only
};

/**
 * A layered network, left to right: a column of round nodes per layer, the
 * nodes spread down the height, a line per connection. `weights[k]` joins
 * layer k to layer k + 1, row-major, layers[k+1].n × layers[k].n:
 * weights[k][j * layers[k].n + i] is from node i to node j. Each line is
 * `chart.1` for a positive weight and `chart.2` for a negative one, thicker
 * and more opaque the larger it is next to the largest between those two
 * layers; below 8 % of it, a line is not drawn. A null `weights`, or a null
 * `weights[k]`, draws every connection alike, thin, in `border.dim`.
 *
 * Nodes grow with the zoom, but as its square root, so crowded layers come
 * apart as one zooms in.
 */
void NetDiagram(Context& ctx, const char* id, const Rect& r, const NetLayer* layers, int nlayers,
                const float* const* weights, int flags = DIAG_HINT);

/** Back to the whole picture: for new content (another formula, another network). */
void ResetDiagram(Context& ctx, const char* id);

// ---- Overlays (§8.6, §8.9, §8.10, §8.11) ----------------------------------------
//
// Everything below draws into the overlay grid and band (Context::beginOverlay),
// so it stays on top however the calls are ordered, and it escapes the clip of
// the panel it was opened from.

/**
 * Attaches a tooltip to the LAST widget: after ~450 ms of hover it appears by
 * the pointer and flips at the window's edges (§8.11). Lines split on '\n'.
 */
void Tooltip(Context& ctx, const char* text);

/**
 * ▌ Normal            ▼▌ — a field that opens a list below itself (above, if
 * there is no room). The current item carries a ✓. ↑/↓ and Enter work while
 * it is open, Esc or a click elsewhere closes it. True when `sel` changed.
 */
bool Dropdown(Context& ctx, const char* id, int w, const char* const* items, int n, int& sel);

enum MenuFlags : int {
    MENU_NONE     = 0,
    MENU_DANGER   = 1 << 0,
    MENU_DISABLED = 1 << 1,
};

/**
 * A right-click menu (§8.10) for the area `r`: a right press inside it opens
 * the menu at the pointer. Between Begin and End go MenuItem / MenuCheck /
 * MenuSeparator / BeginSubmenu. A chosen item closes the whole chain; Esc
 * closes the innermost menu; a click outside closes all of it.
 *
 *     if (ui::BeginContextMenu(ctx, "fs", treeRect)) {
 *         if (ui::MenuItem(ctx, "Open", "Enter")) open();
 *         ui::MenuSeparator(ctx);
 *         if (ui::BeginSubmenu(ctx, "Sort by")) { ...; ui::EndSubmenu(ctx); }
 *         ui::EndContextMenu(ctx);
 *     }
 *
 * The menu is sized from the previous frame's items; the very first frame
 * after opening only measures, which is 16 ms nobody can see.
 */
bool BeginContextMenu(Context& ctx, const char* id, const Rect& r);
void EndContextMenu(Context& ctx);
bool MenuItem(Context& ctx, const char* label, const char* shortcut = nullptr,
              int flags = MENU_NONE);
bool MenuCheck(Context& ctx, const char* label, bool& v);
void MenuSeparator(Context& ctx);
bool BeginSubmenu(Context& ctx, const char* label);
void EndSubmenu(Context& ctx);
/**
 * One title of a menu bar. A click opens the menu `menuId` right under it —
 * draw that menu with BeginContextMenu(ctx, menuId, Rect{}) in the same id
 * scope; the empty rectangle means no right-click opens it. While a bar
 * menu is open, hovering another title switches to its menu.
 */
void MenuBarItem(Context& ctx, const char* label, const char* menuId);
/** Opens a context menu from code (a keyboard shortcut, a toolbar button). */
void OpenContextMenu(Context& ctx, const char* id, int x, int y);

enum ModalFlags : int {
    MODAL_NONE   = 0,
    MODAL_DANGER = 1 << 0,   // frame and title in `danger` — an irreversible question;
                             // the window under it jolts as it opens (§9)
};

/**
 * A centred modal (§8.9): single frame like everything else, no shadow, the
 * window beneath dimmed rather than washed out. While it is open the
 * rest of the window takes no input and Tab cycles inside it only. Esc sets
 * `open` to false. Returns the inside region's use: call EndModal() only when
 * it returned true.
 */
bool BeginModal(Context& ctx, const char* id, const char* title, int w, int h, bool& open,
                int flags = MODAL_NONE);
void EndModal(Context& ctx);

// ---- The error glitch (§9) ------------------------------------------------------------

/**
 * The one cyberpunk liberty (§9): the moment `error` turns true, the cells
 * of `r` jolt for ~0.2 s — every row shifts sideways by 1–2 cells and about a
 * third of its glyphs turn to ░▒▓█▀▄ noise in `danger` — in three timed steps, then
 * settle. A reaction to the event, not a standing effect: staying in error
 * does not repeat it — except in a showcase (Context::forced(), the States
 * screen), where it loops with a 0.2 s pause so it can be seen. Nothing plays
 * with glitches switched off (Context::setGlitches). Call AFTER drawing the
 * widget, every frame, with the widget's own id and its error state.
 * Returns true while it is playing.
 */
bool Glitch(Context& ctx, Id owner, const Rect& r, bool error);

// ---- Sidebar (§8.18) ----------------------------------------------------------------

/** One section on the sidebar: a raster icon and its tooltip. */
struct SidebarItem {
    int         icon = -1;          // raster id (a white mask); -1 draws a ■ instead
    const char* tip = nullptr;      // the tooltip: the section's name
};

/** The sidebar's width in columns (§8.18). */
constexpr int SIDEBAR_W = 4;

/**
 * Where a ─ rule's stroke sits in its row, as a fraction from the row's top
 * (the font draws ─ at 320..420 of a 1320-unit line, baseline at 1020: its
 * middle is 0.49 of the row). Something that has to MEET a rule drawn in the
 * row above or below — the sidebar's ground — reaches this far into it.
 */
constexpr float RULE_MID = 0.49f;

/**
 * A vertical strip of section icons on the left edge, SIDEBAR_W columns wide,
 * as tall as `r`: its own `sidebar.bg` ground, no edge line. Each section is
 * a two-row slot with its icon centred in it, one row tall; slots that do not
 * fit are dropped.
 *
 * The active section's icon is `sidebar.active` (`border.focus` while the
 * strip has the keyboard), the one under the pointer `sidebar.hover`, the
 * rest `sidebar.icon` — colour only, no fill (§8.18); all four are theme
 * slots. Every icon has its tooltip.
 * One Tab stop; ↑/↓/Home/End move while it is focused.
 *
 * @param iconPx the icons' pixel size (IconStrip::px): drawn at exactly that
 *        size on whole pixels, never scaled.
 * @param bleedTop/bleedBottom how far, in rows, the ground reaches past `r` — to meet a ─ rule in the row above (1 − RULE_MID)
 *        or below (RULE_MID) instead of stopping half a row short. 0 when the
 *        sidebar runs to the window's edge.
 * @return true when `active` changed.
 */
bool Sidebar(Context& ctx, const char* id, const Rect& r,
             const SidebarItem* items, int n, int& active, int iconPx,
             float bleedTop = 0.0f, float bleedBottom = 0.0f);

// ---- Tabs (§8.15) -------------------------------------------------------------------

/**
 * A panel whose title row is a row of tabs; the active one is sunk into the
 * frame like a panel title, the others "break out" of it:
 *
 *     ┌─[ WIDGETS ]─┐ DATA ├─┐ INPUT ├──────────┐
 *     │             └──────┘ └───────┘          │
 *
 * Click or ←/→ on the focused strip switches. Returns the inside (frame to
 * frame, from the row under the tab bottoms); pair with EndTabs().
 */
Rect BeginTabs(Context& ctx, const char* id, const Rect& r,
               const char* const* labels, int n, int& active);
void EndTabs(Context& ctx);

} // namespace ui
