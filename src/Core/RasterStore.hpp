#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Raster elements (docs/DESIGN_SYSTEM.md §16).
//
// The system draws its own widgets with glyphs and geometry; this is the one
// way a real picture gets on screen — a thumbnail, a preview, a logo. Images
// are packed into a single RGBA page, separate from the glyph atlas: that one
// is single-channel and is rebuilt whenever the density changes, and pictures
// have no business being thrown away with it.
//
// Because the page is its own texture unit rather than its own draw call, the
// frame is still a single glDrawArrays (see Renderer). Only a picture too
// large for the page forces a second one, and the store says so rather than
// silently dropping it.
// ---------------------------------------------------------------------------

struct RasterInfo {
    int  ax = 0, ay = 0, aw = 0, ah = 0;  // position in the page, in texels
    bool valid = false;
};

class RasterStore {
public:
    /** Allocates the page. 1024×1024 RGBA is 4 MB — one allocation, no growth. */
    bool init(int pageW = 1024, int pageH = 1024);

    /**
     * Adds an image decoded from file bytes (PNG/JPG/etc, via stb_image).
     * Returns the id to hand to CellBuffer::sprite(), or -1 if it does not
     * fit or cannot be decoded.
     */
    int addFromMemory(const uint8_t* bytes, size_t n);

    /** Adds raw, tightly packed RGBA. Same contract as addFromMemory(). */
    int addRGBA(const uint8_t* rgba, int w, int h);

    /**
     * Decodes a horizontal strip of `n` equal square-or-not cells and adds
     * each as its own image; their ids go to `outIds[0..n-1]`. Returns the
     * cell width in pixels, or 0 on failure (then no id is written). The
     * caption icon sheets are loaded this way.
     */
    int addStripFromMemory(const uint8_t* bytes, size_t size, int n, int* outIds);

    /**
     * Redraws image `id` in place: `rgba` is tightly packed, of the size it
     * was added with. For a picture that changes every frame — an animation
     * drawn on the CPU — so the page does not fill up with old frames, and
     * only this rectangle goes to the GPU, not the whole page.
     */
    bool update(int id, const uint8_t* rgba);

    const RasterInfo& get(uint16_t id) const;
    int count() const { return (int)items_.size(); }

    int texW() const { return page_w_; }
    int texH() const { return page_h_; }
    const uint8_t* pixels() const { return page_.data(); }

    bool dirty() const { return dirty_; }
    /**
     * What changed since clearDirty(): the whole page after init() or an
     * add, else the bounding box of the updates, half-open, in texels.
     */
    bool dirtyAll() const { return dirty_all_; }
    void dirtyRect(int& x0, int& y0, int& x1, int& y1) const {
        x0 = dx0_; y0 = dy0_; x1 = dx1_; y1 = dy1_;
    }
    void clearDirty() { dirty_ = dirty_all_ = false; }

private:
    bool place(int w, int h, int& outX, int& outY);

    std::vector<uint8_t>    page_;      // RGBA
    std::vector<RasterInfo> items_;
    int  page_w_ = 0, page_h_ = 0;
    int  pen_x_ = 0, pen_y_ = 0, row_h_ = 0;
    bool dirty_ = false, dirty_all_ = false;
    int  dx0_ = 0, dy0_ = 0, dx1_ = 0, dy1_ = 0;
};
