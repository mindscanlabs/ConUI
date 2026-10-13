#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The screen (docs/DESIGN_SYSTEM.md §2). Not one grid but an ordered stack:
// a grid of character cells PLUS three lists that composite around it.
//
//   surfaces  — window / panel / titlebar fills, whole cell rectangles
//   cell bg   — per-cell fills: selection, sunken fields, raised headers
//   shapes    — sub-cell geometry: scrollbar thumbs, progress fills, caption
//               boxes, splitter grips (§2.1)
//   glyphs    — frames and text, always opaque
//   sprites   — raster elements (§16)
//
// fg/bg are indices into the theme palette, never literal colours — that is
// what makes re-theming a one-file change and what lets a swatch in the theme
// editor paint itself simply by using its own slot as fg (§8.16).
// ---------------------------------------------------------------------------

enum CellAttr : uint8_t {
    ATTR_NONE  = 0,
    ATTR_BOLD  = 1 << 0,  // synthesised by redrawing the glyph 1 px right (§7)
    ATTR_DIM   = 1 << 1,
    ATTR_UNDER = 1 << 2,
};

struct Cell {
    uint32_t cp   = ' ';
    uint8_t  fg   = 8;    // TEXT
    uint8_t  bg   = 0;    // BG_WINDOW
    uint8_t  attr = ATTR_NONE;
};

/**
 * Which band an item composites in. Overlays finish on top of the window's
 * own content as a whole — without this a scrollbar thumb, which lives in the
 * shape list rather than in a cell, paints straight through a modal.
 */
enum ZBand : uint8_t { Z_CONTENT = 0, Z_OVERLAY = 1 };

/**
 * The smooth bar's height (a fraction of the row, centred in it) and corner
 * radius (a fraction of its shorter side): the progress bar's (§8.3), and so
 * also the switch's (§8.5) and the chart bars' (§8.20).
 */
constexpr float BAR_H = 0.62f;
constexpr float BAR_R = 0.14f;

/**
 * A light frame line (─ │ ┌ …) inside its cell, in pixels: its thickness —
 * the font's own stroke, 100 of 600 units of the cell width — and where the
 * vertical stroke's left edge and the horizontal stroke's top edge fall. One
 * formula for everything that must meet a frame line on the same pixels: the
 * atlas that draws the lines (§3), a panel's background cut to its frame
 * (§8.1), the slider and the charts' ticks.
 */
struct BoxLine { int stroke, x0, y0; };
inline BoxLine BoxLineIn(int cellW, int cellH) {
    const int s = cellW * 100 / 600 + ((cellW * 100 % 600) * 2 >= 600 ? 1 : 0);   // rounded
    const int stroke = s < 1 ? 1 : s;
    const int x0 = (int)(cellW * 0.5f - stroke * 0.5f + 0.5f);
    const int y0 = (int)(cellH * 0.49f + 0.5f) - stroke / 2;           // where the font's ─ runs
    return BoxLine{ stroke, x0, y0 };
}

/**
 * A whole-cell rectangle of background: the window, a panel, a title bar.
 * `framed` marks the edges that carry a frame line: there the background
 * stops at the line's outer edge instead of the cell's, so a panel's ground
 * never shows outside its frame. An edge cut by a clip is not framed.
 *
 * EDGE_CAP is the other half of that: a one-row surface over a title sunk
 * into a frame line — "[ TITLE ]", a tab's label — from the top of its
 * cells down to the line's TOP edge, exactly where the panel's own ground
 * begins, so the text sits on ground without the two overlapping (§8.1).
 *
 * EDGE_INK_L / EDGE_INK_R stop a cap at the glyph in its first / last cell
 * instead: at the ink of the `[` and `]` round a title, left, right and top,
 * so the ground stays inside the brackets.

 */
enum : uint8_t { EDGE_L = 1, EDGE_R = 2, EDGE_T = 4, EDGE_B = 8, EDGE_ALL = 15, EDGE_CAP = 16,
                 EDGE_INK_L = 32, EDGE_INK_R = 64 };
struct Surface {
    int16_t x = 0, y = 0, w = 0, h = 0;
    uint8_t slot = 0;
    uint8_t z = Z_CONTENT;
    uint8_t framed = 0;
};

/**
 * A clip rectangle in whole cells, half-open. Shapes and sprites carry one
 * each instead of being cut on the CPU: the renderer trims the QUAD, but the
 * shape's own geometry — the rounded-rect SDF, the image's UVs — is still
 * computed from the full, uncut rectangle. So a pill that runs off the edge
 * of a scroll region is cut straight at the edge, not re-rounded there.
 */
struct ClipRect {
    int16_t x0 = -32768, y0 = -32768, x1 = 32767, y1 = 32767;
};

/**
 * A rectangle in CELL-FRACTION coordinates — x = 2.5 is halfway across the
 * third column. Fractions rather than pixels so the same numbers are correct
 * at any density and any DPI.
 *
 * `rfrac` is the corner radius as a fraction of the shorter side (0 square,
 * 0.5 a pill); `stroke_px` above zero turns the fill into an outline of that
 * many pixels. Coverage is evaluated analytically in the fragment shader.
 *
 * A SHAPE_SEGMENT is a straight line with round ends from (x, y) to
 * (x + w, y + h) — `w`/`h` are the run and may be negative — `stroke_px`
 * thick. The charts draw their lines with it (§8.20).
 */
enum ShapeKind : uint8_t { SHAPE_RECT = 0, SHAPE_SEGMENT = 1 };
struct Shape {
    float   x = 0, y = 0, w = 0, h = 0;
    float   rfrac = 0.0f;
    float   alpha = 1.0f;
    float   stroke_px = 0.0f;
    uint8_t slot = 0;
    uint8_t z = Z_CONTENT;
    ClipRect clip;
    uint8_t kind = SHAPE_RECT;
};

/**
 * A raster element (§16): a box in cell fractions plus an image id. `tint`
 * is a palette slot the picture's colour is multiplied by — kNoTint (the
 * default) draws it as it is; the caption icons are white masks tinted this
 * way, so they follow the theme and the hover/pressed states.
 */
constexpr uint8_t kNoTint = 0xFF;
struct SpriteRef {
    float    x = 0, y = 0, w = 0, h = 0;
    float    alpha = 1.0f;
    uint16_t id = 0;
    uint8_t  z = Z_CONTENT;
    ClipRect clip;
    uint8_t  tint = kNoTint;
};

class CellBuffer {
public:
    void resize(int w, int h, uint8_t fill_bg);
    void clear(uint8_t fill_bg);

    int width()  const { return w_; }
    int height() const { return h_; }
    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    /** In the buffer AND inside the current clip — what every writer checks. */
    bool writable(int x, int y) const {
        return in(x, y) && x >= clip_.x0 && y >= clip_.y0 && x < clip_.x1 && y < clip_.y1;
    }

    /**
     * The cell in the CURRENT band's grid. Cells come in two grids, like
     * everything else on the screen: the content grid, and an overlay grid
     * that menus, tooltips and modals write while the band is Z_OVERLAY.
     * With two grids the order of drawing stops mattering — a dropdown opened
     * from the left panel stays on top of the panel on the right that is
     * drawn after it in the same frame.
     */
    Cell& at(int x, int y) {
        const size_t i = (size_t)y * w_ + x;
        return band_ == Z_OVERLAY ? over_[i] : cells_[i];
    }
    const Cell& at(int x, int y) const {
        const size_t i = (size_t)y * w_ + x;
        return band_ == Z_OVERLAY ? over_[i] : cells_[i];
    }
    /** The content grid, whatever the band. */
    const Cell& base(int x, int y) const { return cells_[(size_t)y * w_ + x]; }
    /**
     * What is actually on screen at (x, y): the overlay cell if there is one,
     * otherwise the content cell — dimmed, if a modal asked for it (§8.9).
     */
    Cell top(int x, int y) const;

    /**
     * Dims the content grid under a modal: every glyph loses brightness, so
     * the text underneath can still be made out (§8.9); the modal itself
     * draws a solid translucent tint over the whole window to darken the
     * backgrounds. Applied when the screen is composited, so content drawn
     * after the modal is dimmed too.
     */
    void setDimBase(bool on) { dim_base_ = on; }
    bool dimBase() const { return dim_base_; }

    void put(int x, int y, uint32_t cp, uint8_t fg, uint8_t bg, uint8_t attr = ATTR_NONE) {
        if (!writable(x, y)) return;
        at(x, y) = Cell{ cp, fg, bg, attr };
        if (band_ == Z_OVERLAY) over_set_[(size_t)y * w_ + x] = 1;
    }
    void setBg(int x, int y, uint8_t bg) {
        if (!writable(x, y)) return;
        at(x, y).bg = bg;
        if (band_ == Z_OVERLAY) over_set_[(size_t)y * w_ + x] = 1;
    }
    void fill(int x, int y, int w, int h, uint32_t cp, uint8_t fg, uint8_t bg);
    void bgRect(int x, int y, int w, int h, uint8_t bg);

    /** Writes one UTF-8 string starting at (x,y). Returns its width in cells. */
    int text(int x, int y, const char* s, uint8_t fg, uint8_t bg, uint8_t attr = ATTR_NONE);
    /** Same, but the string ENDS at column xr (inclusive). */
    void textRight(int xr, int y, const char* s, uint8_t fg, uint8_t bg, uint8_t attr = ATTR_NONE);

    // --- clipping --------------------------------------------------------
    //
    // Everything written through this class — cells, surfaces, shapes,
    // sprites — respects the current clip. A scroll region is nothing more
    // than a clip plus a shifted cursor; without this, content scrolled out
    // of a panel would simply be drawn over the panel's frame.

    /** Intersects the current clip with a cell rectangle. */
    void pushClip(int x, int y, int w, int h);
    /** Replaces the clip instead of narrowing it — an overlay escapes its panel. */
    void pushClipReset(int x, int y, int w, int h);
    void popClip();
    const ClipRect& clip() const { return clip_; }

    // --- the layers around the grid (§2) --------------------------------

    /** The band that surface()/shape()/sprite() stamp onto new items. */
    void setBand(ZBand b) { band_ = b; }
    ZBand band() const { return band_; }

    /** A background rectangle; `framed` (EDGE_*) cuts those edges to the frame line. */
    void surface(int x, int y, int w, int h, uint8_t slot, uint8_t framed = 0);
    void shape(float x, float y, float w, float h, uint8_t slot,
               float rfrac = 0.0f, float alpha = 1.0f, float stroke_px = 0.0f);
    /** A line from (x0, y0) to (x1, y1), in cell fractions, `thick_px` thick. */
    void segment(float x0, float y0, float x1, float y1, uint8_t slot, float thick_px,
                 float alpha = 1.0f);
    void sprite(float x, float y, float w, float h, uint16_t id, float alpha = 1.0f,
                uint8_t tint = kNoTint);

    /**
     * The screen glitch (§9): shifts rows [y0, y1) sideways by `dx` cells —
     * the content grid, and the overlay grid too if `overlays` — together
     * with the shapes and sprites whose middle lies in those rows. Cells that
     * slide in from the edge are empty window. Surfaces (panel grounds) span
     * many rows and stay put, which only adds to the tear.
     */
    void glitchRows(int y0, int y1, int dx, bool overlays);

    /**
     * Row `y`, columns x .. x+w-1, is the top frame of a panel that sits
     * under a horizontal seam (§8.17): the renderer draws that row — its
     * glyphs, its cap of ground, the top edge of the panel's ground — raised
     * by half the gap between the seam's line and the frame line, and runs
     * the frame's verticals up to it. Only the drawing moves; the cells, and
     * so the pointer, stay where they are. Cleared every frame.
     */
    // How far: RAISE_FRAME is the top frame under a seam (above); RAISE_HALF
    // half a row — the rule over the status bar (§8.1); RAISE_QUARTER a
    // quarter row — the bottom of an inactive tab, closer under its label (§8.15).
    enum RaiseBy : uint8_t { RAISE_FRAME = 0, RAISE_HALF = 1, RAISE_QUARTER = 2 };
    struct Raise { int16_t y, x0, x1; uint8_t by; };
    void raiseRow(int y, int x, int w, RaiseBy by = RAISE_FRAME) {
        if (w > 0) raises_.push_back(Raise{ (int16_t)y, (int16_t)x, (int16_t)(x + w), (uint8_t)by });
    }
    const std::vector<Raise>& raises() const { return raises_; }
    /** True when (x, y) shows an overlay cell rather than the content grid. */
    bool overlaid(int x, int y) const { return over_set_[(size_t)y * w_ + x] != 0; }

    const std::vector<Surface>&   surfaces() const { return surfaces_; }
    const std::vector<Shape>&     shapes()   const { return shapes_; }
    const std::vector<SpriteRef>& sprites()  const { return sprites_; }

    /**
     * The whole screen as UTF-8 text, one line per row.
     * This is the backbone of the snapshot tests: a layout
     * regression shows up as a text diff, with no screenshots involved.
     *
     * It deliberately sees only the cells. Shapes and sprites are not text and
     * are checked by their own tests.
     */
    std::string dump() const;

private:
    int w_ = 0, h_ = 0;
    std::vector<Cell> cells_;
    std::vector<Cell> over_;
    std::vector<uint8_t> over_set_;
    bool dim_base_ = false;
    std::vector<Surface>   surfaces_;
    std::vector<Shape>     shapes_;
    std::vector<SpriteRef> sprites_;
    std::vector<Raise>     raises_;
    ZBand band_ = Z_CONTENT;
    ClipRect clip_;
    std::vector<ClipRect> clip_stack_;

    void clearLayers();
};

/** Decodes UTF-8 into codepoints. Invalid bytes become U+FFFD. */
std::vector<uint32_t> Utf8Decode(const char* s);
/** Width of a UTF-8 string in cells. */
int Utf8Width(const char* s);
/** Appends one codepoint to a UTF-8 string. */
void Utf8Append(std::string& out, uint32_t cp);
