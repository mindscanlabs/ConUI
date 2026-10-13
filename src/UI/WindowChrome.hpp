#pragma once
#include <cstdint>

class CellBuffer;

// ---------------------------------------------------------------------------
// The caption ConUI draws itself, inside the client area the OS hands over
// (docs/DESIGN_SYSTEM.md §8.1).
//
// Flat, Macintosh System 7: an empty close box on the left, collapse / zoom /
// fullscreen on the right, the title centred, on a raised band. No rule
// underneath: the band itself ends the caption, and the menu bar
// below has its own. No stripes: they were tried and removed, being the
// noisiest thing on a dense screen.
//
// Focus is carried the System 7 way: an inactive window loses its boxes and
// dims its title. Row 0 is the caption and nothing else; row 1 stays clear so
// the content cannot look tucked under it.
// ---------------------------------------------------------------------------

namespace chrome {

enum Button : int {
    BTN_NONE = -1,
    BTN_CLOSE = 0,
    BTN_COLLAPSE,      // minimise
    BTN_ZOOM,          // maximise / restore
    BTN_FULLSCREEN,
    BTN_COUNT
};

/** Pixel span of one caption box, measured from the window's left edge. */
struct ButtonSpan { int x0, x1; };

/**
 * The caption box icons for one pixel size: raster ids of white masks from
 * resources.dat (`caption_<px>.png`, a strip of close | collapse | zoom |
 * fullscreen | filled), tinted with a theme slot when drawn. -1 = not loaded.
 */
struct CaptionIcons {
    int id[BTN_COUNT + 1] = { -1, -1, -1, -1, -1 };
    int px = 0;                                   // their size, pixels
    bool valid() const {
        for (int v : id) if (v < 0) return false;
        return px > 0;
    }
};
constexpr int ICON_FILLED = BTN_COUNT;           // the pressed state of any box

/** How big a caption box is for this cell height, in whole pixels. */
int CaptionBoxPx(int cellH);

/** Rows the caption owns. Content starts at CONTENT_TOP. */
constexpr int CAPTION_ROW = 0;
constexpr int CLEAR_ROW   = 1;
constexpr int CONTENT_TOP = 2;

/**
 * Draws the caption into row 0.
 * @param cellW/cellH the real cell size — the boxes are square in PIXELS and
 *        the cell is not, so they cannot be laid out without it.
 * @param hover which box the pointer is over, or BTN_NONE.
 * @param pressed which box is held down, or BTN_NONE.
 * @param spillPx the leftover pixels to the right of the last whole column
 *        (AppWindow::spillX()) — the grid is floor(size/cell), and without
 *        this the caption's own band stopped short of the true right edge
 *        after a resize that did not land on a whole number of cells.
 * @param icons the raster icons for the boxes. Drawn at their own pixel size
 *        on whole pixels, never scaled. Null or incomplete: the boxes are
 *        drawn with geometry instead, as before.
 */
void DrawCaption(CellBuffer& cb, const char* title, bool focused,
                 int cellW, int cellH, int hover = BTN_NONE, int pressed = BTN_NONE,
                 int spillPx = 0, const CaptionIcons* icons = nullptr);

/**
 * Where the four boxes land, in pixels, for a grid `cols` wide with the given
 * cell size. Fills `out[BTN_COUNT]`. The platform layer needs these so clicks
 * on a box are not swallowed as "drag the window", and the app needs them to
 * hit-test the pointer.
 */
void ButtonSpans(int cols, int cellW, int cellH, ButtonSpan out[BTN_COUNT]);

/** Which box a client-area pixel is over, or BTN_NONE. */
int HitTest(int px, int py, int cols, int cellW, int cellH);

/** Height of the caption band in pixels — a third taller than one cell row,
 * the same figure the platform layer needs for its drag/resize
 *  hit-testing and the OS caption-eating rectangle. */
int CaptionHeightPx(int cellH);

/**
 * How far the caption's own row is nudged down within its (taller) band so
 * the title sits centred with padding above and below, instead of flush to
 * the band's top. The renderer applies this to row 0's glyphs only.
 */
float CaptionTextPadPx(int cellH);

/**
 * An edge-to-edge `border.dim` rule on the window background at row `y`:
 * under the menu bar, above the status bar. `raised` draws it half a row up —
 * the rule over the status bar, so it sits as close under the panels above it
 * as a seam's line does (§8.1); the cells do not move.
 */
void DrawRule(CellBuffer& cb, int y, bool raised = false);

} // namespace chrome
