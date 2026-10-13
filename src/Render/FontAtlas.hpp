#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Glyph cache, rasterized with the scale locked to the integer cell
// (docs/DESIGN_SYSTEM.md §3). This is the whole reason the frames tile without
// seams, so the arithmetic here is the load-bearing part of the renderer:
//
//   sx       = cell_w / advance_units          (e.g. 9 / 600)
//   sy       = cell_h / lineHeight_units       (e.g. 20 / 1320)
//   baseline = ascent_units * sy               kept FRACTIONAL, never rounded
//
// The glyph is then rasterized with that fractional part as a subpixel shift.
// Because cell_h is an integer and the baseline offset inside the cell is
// constant, the shift is the same for every row on screen — so each glyph is
// rasterized once and cached, with no per-position cost.
//
// Glyph quads are NOT clipped to the cell. The light straight box-drawing
// lines (─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼ ╴╵╶╷) are not taken from the font at all: they
// are drawn to exactly one cell on whole pixels (drawBox), because the
// font's │ overhangs the cell (bbox y[-400..1120] in a 1320-unit cell) and the
// overlapping antialiased edges showed as faint bands at every joint.
// ---------------------------------------------------------------------------

struct GlyphInfo {
    // Position in the atlas, in texels.
    int   ax = 0, ay = 0, aw = 0, ah = 0;
    // Offset of the bitmap's top-left from the cell's top-left, in pixels.
    int   dx = 0, dy = 0;
    bool  blank = true;
};

class FontAtlas {
public:
    /**
     * @param ttf        font file bytes (kept alive internally)
     * @param cellW/cellH integer cell size in pixels
     */
    bool init(std::vector<uint8_t> ttf, int cellW, int cellH);

    /** Rasterizes on first use. Never returns null once init() succeeded. */
    const GlyphInfo& glyph(uint32_t cp);

    /** True if the font's cmap actually has this codepoint. */
    bool hasGlyph(uint32_t cp) const;

    /** True for a drawn frame glyph with a stroke down to the cell's bottom (│ ┌ ┐ ├ ┤ ┬ ┼ ╷). */
    static bool hasDownArm(uint32_t cp);

    int cellW() const { return cell_w_; }
    int cellH() const { return cell_h_; }
    int texW()  const { return tex_w_; }
    int texH()  const { return tex_h_; }
    const uint8_t* pixels() const { return pixels_.data(); }

    /** True when glyphs were added since the last clearDirty(). */
    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

    // Metrics read straight out of the font, for the self-test.
    int advanceUnits()   const { return adv_units_; }
    int lineHeightUnits() const { return line_units_; }
    int ascentUnits()    const { return asc_units_; }

private:
    bool pack(int w, int h, int& outX, int& outY);
    /** Light straight box-drawing lines, drawn to the cell instead of rasterized. */
    bool drawBox(uint32_t cp, GlyphInfo& info);
    /** A glyph not yet in the cache: drawn, rasterized, or the missing-glyph box. */
    GlyphInfo rasterize(uint32_t cp);

    std::vector<uint8_t> ttf_;
    // stbtt_fontinfo is stashed as raw bytes so this header stays free of
    // stb_truetype.h; only FontAtlas.cpp needs the implementation.
    std::vector<uint8_t> fontinfo_;

    int cell_w_ = 0, cell_h_ = 0;
    int adv_units_ = 0, line_units_ = 0, asc_units_ = 0;
    float sx_ = 0, sy_ = 0;
    int   baseline_i_ = 0;
    float baseline_f_ = 0;

    int tex_w_ = 1024, tex_h_ = 1024;
    std::vector<uint8_t> pixels_;      // R8
    int pen_x_ = 1, pen_y_ = 1, row_h_ = 0;
    bool dirty_ = false;

    std::unordered_map<uint32_t, GlyphInfo> cache_;
    // Direct pointers into cache_ for the codepoints the screen is made of
    // (Latin, Cyrillic, box drawing, blocks, shapes): no hashing per cell.
    static constexpr uint32_t kFastGlyphs = 0x3000;
    std::vector<const GlyphInfo*> fast_;
    GlyphInfo blank_{};
};
