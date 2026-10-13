#pragma once
#include "CellBuffer.hpp"
#include "FontAtlas.hpp"
#include "GLLoader.hpp"
#include "../Core/RasterStore.hpp"
#include "../UI/Theme.hpp"

#include <unordered_map>

// ---------------------------------------------------------------------------
// The layer stack -> one glDrawArrays (docs/DESIGN_SYSTEM.md §2).
//
// Five kinds of quad go into the same vertex stream and the same program,
// picked apart by a per-vertex mode:
//   * MODE_FLAT   — surfaces and cell backgrounds, which may be translucent
//                   (this is what lets the desktop show through, §6);
//   * MODE_GLYPH  — atlas coverage, always fully opaque so text stays
//                   readable over any backdrop;
//   * MODE_SHAPE  — a rounded rectangle, filled or stroked, evaluated
//                   analytically in the fragment shader (§2.1);
//   * MODE_SPRITE — an RGBA raster element (§16);
//   * MODE_SEGMENT — a straight line with round ends, the charts' lines (§8.20).
//
// The raster page is a second texture UNIT, not a second draw call, so the
// frame still costs exactly one glDrawArrays — the layer ordering lives in
// the order of the vertices, and blending does the rest.
// ---------------------------------------------------------------------------

class Renderer {
public:
    bool init();
    void shutdown();

    /** Vertices in the last frame — the one draw call's size, for the status bar. */
    size_t lastVertexCount() const { return last_verts_; }

    /** Uploads the atlas page if new glyphs were rasterized since last frame. */
    void syncAtlas(FontAtlas& atlas);
    /** Uploads the raster page if images were added since last frame. */
    void syncRasters(RasterStore& rasters);

    /**
     * @param fbW/fbH framebuffer size in pixels
     * @param ox/oy   pixel offset of the grid's top-left inside the framebuffer
     * @param rasters may be null when the scene has no sprites
     * @param ctx     identifies the CURRENT GL context. Programs, buffers and
     *                textures are shared between contexts; vertex array
     *                objects are NOT, so every context needs its own. Pass the
     *                window (or any stable per-context pointer) when the app
     *                has more than one; nullptr is fine for a single context.
     * @param windowAlpha 0..1, multiplies the window background's own alpha
     *                (§6). It covers the WHOLE framebuffer, not just the cell
     *                grid, so the leftover strip along the right/bottom edge
     *                (§8.1's "spill") is not left at a different opacity.
     * @param row0PadPx pixels to nudge row 0's GLYPHS down — the caption's
     *                title sits centred in a band a third taller than a row
     *                (§8.1). 0 when row 0 is ordinary content (fullscreen).
     * @param panelAlpha 0..1, multiplies the alpha of the SECTION background
     *                (`bg.panel` in the content band) — independent of the
     *                window background, and never touching frames or text,
     *                which are glyphs and always opaque (§6).
     */
    void draw(const CellBuffer& cells, FontAtlas& atlas, const Theme& theme,
              int fbW, int fbH, int ox = 0, int oy = 0,
              RasterStore* rasters = nullptr, void* ctx = nullptr,
              float windowAlpha = 1.0f, float row0PadPx = 0.0f,
              float panelAlpha = 1.0f);

private:
    enum Mode : int { MODE_FLAT = 0, MODE_GLYPH = 1, MODE_SHAPE = 2, MODE_SPRITE = 3, MODE_SEGMENT = 4 };

    struct Vertex {
        float x, y;             // pixels
        float u, v;             // atlas texels normalised, or local px for shapes
        float r, g, b, a;       // 0..1
        float mode;
        float hw, hh;           // shape half-extents, px
        float radius, stroke;   // shape corner radius and outline width, px
    };

    void pushQuad(float x, float y, float w, float h,
                  float u0, float v0, float u1, float v1,
                  const Rgba& c, float alphaScale, Mode mode);
    void pushShape(const Shape& s, const Rgba& c, int cw, int ch, float ox, float oy);
    void pushSegment(const Shape& s, const Rgba& c, int cw, int ch, float ox, float oy);

    /** The raise of a panel top frame at cell (x, y), px: raise_px_ inside a raised span, else 0. */
    int raiseAt(const CellBuffer& cells, int x, int y) const {
        if (y < 0 || y >= (int)raised_rows_.size() || !raised_rows_[(size_t)y]) return 0;
        for (const CellBuffer::Raise& r : cells.raises())
            if (r.y == y && x >= r.x0 && x < r.x1)
                return r.by == CellBuffer::RAISE_HALF ? half_px_
                     : r.by == CellBuffer::RAISE_QUARTER ? half_px_ / 2 : raise_px_;
        return 0;
    }

    /** A slot's colour for a layer, with the section alpha applied. */
    Rgba layerColor(const Theme& theme, uint8_t slot, uint8_t band) const;
    void emitSurfaces(const CellBuffer&, const Theme&, int cw, int ch, int ox, int oy, uint8_t band);
    void emitShapes  (const CellBuffer&, const Theme&, int cw, int ch, int ox, int oy, uint8_t band);
    void emitSprites (const CellBuffer&, const Theme&, const RasterStore*, int cw, int ch, int ox, int oy, uint8_t band);

    /** The VAO belonging to this context, created and configured on demand. */
    GLuint vaoFor(void* ctx);

    GLuint prog_ = 0, vbo_ = 0, tex_ = 0, rtex_ = 0;
    GLint  u_viewport_ = -1, u_atlas_ = -1, u_raster_ = -1;
    int    tex_w_ = 0, tex_h_ = 0;
    int    rtex_w_ = 0, rtex_h_ = 0;
    std::vector<Vertex> verts_;
    size_t last_verts_ = 0;
    float  panel_alpha_ = 1.0f;
    int    raise_px_ = 0;          // this frame's raise of a top frame under a seam
    int    half_px_ = 0;           // half a row, for RAISE_HALF (a quarter: half of it)
    std::vector<uint8_t> raised_rows_;   // this frame: 1 for a row with any raise in it
    // this frame, per band: the slot of the framed surface under each cell
    static constexpr uint8_t kNoGround = 0xFF;
    std::vector<uint8_t> ground_[2];
    FontAtlas* atlas_ = nullptr;   // this frame's atlas: a title's cap stops at its brackets' ink
    std::unordered_map<void*, GLuint> vaos_;
};
