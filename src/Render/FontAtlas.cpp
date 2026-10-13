#include "FontAtlas.hpp"
#include "CellBuffer.hpp"   // BoxLineIn
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../Core/stb_truetype.h"

namespace {
    stbtt_fontinfo* fi_of(std::vector<uint8_t>& storage) {
        return reinterpret_cast<stbtt_fontinfo*>(storage.data());
    }
    const stbtt_fontinfo* fi_of(const std::vector<uint8_t>& storage) {
        return reinterpret_cast<const stbtt_fontinfo*>(storage.data());
    }
}

bool FontAtlas::init(std::vector<uint8_t> ttf, int cellW, int cellH) {
    if (ttf.empty() || cellW <= 0 || cellH <= 0) return false;
    ttf_ = std::move(ttf);
    fontinfo_.assign(sizeof(stbtt_fontinfo), 0);

    stbtt_fontinfo* fi = fi_of(fontinfo_);
    if (!stbtt_InitFont(fi, ttf_.data(), stbtt_GetFontOffsetForIndex(ttf_.data(), 0)))
        return false;

    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(fi, &asc, &desc, &gap);
    int adv = 0, lsb = 0;
    // 'M' rather than a random glyph: the font is monospaced, so every glyph
    // has the same advance, but 'M' is guaranteed to exist.
    stbtt_GetCodepointHMetrics(fi, 'M', &adv, &lsb);
    if (adv <= 0) return false;

    asc_units_  = asc;
    adv_units_  = adv;
    line_units_ = asc - desc + gap;
    if (line_units_ <= 0) return false;

    cell_w_ = cellW;
    cell_h_ = cellH;
    sx_ = (float)cell_w_ / (float)adv_units_;
    sy_ = (float)cell_h_ / (float)line_units_;
    const float baseline = asc_units_ * sy_;
    baseline_i_ = (int)std::floor(baseline);
    baseline_f_ = baseline - baseline_i_;

    // One 1024² R8 page holds the Latin + Cyrillic + box/block working set
    // several times over; growing it is a later problem, and pack() reports
    // when it would be needed.
    pixels_.assign((size_t)tex_w_ * tex_h_, 0);
    pen_x_ = 1; pen_y_ = 1; row_h_ = 0;
    cache_.clear();
    fast_.assign(kFastGlyphs, nullptr);
    dirty_ = true;
    return true;
}

bool FontAtlas::hasGlyph(uint32_t cp) const {
    if (fontinfo_.empty()) return false;
    return stbtt_FindGlyphIndex(fi_of(fontinfo_), (int)cp) != 0;
}

bool FontAtlas::pack(int w, int h, int& outX, int& outY) {
    if (w > tex_w_ - 2) return false;
    if (pen_x_ + w + 1 > tex_w_) {      // next shelf
        pen_x_ = 1;
        pen_y_ += row_h_ + 1;
        row_h_ = 0;
    }
    if (pen_y_ + h + 1 > tex_h_) return false;
    outX = pen_x_;
    outY = pen_y_;
    pen_x_ += w + 1;
    if (h > row_h_) row_h_ = h;
    return true;
}

namespace {

enum Arm : uint8_t { ARM_L = 1, ARM_R = 2, ARM_U = 4, ARM_D = 8 };

/** The arms of a light straight box-drawing glyph, or 0 for any other codepoint. */
uint8_t boxArms(uint32_t cp) {
    switch (cp) {
    case 0x2500: return ARM_L | ARM_R;                  // ─
    case 0x2502: return ARM_U | ARM_D;                  // │
    case 0x250C: return ARM_R | ARM_D;                  // ┌
    case 0x2510: return ARM_L | ARM_D;                  // ┐
    case 0x2514: return ARM_R | ARM_U;                  // └
    case 0x2518: return ARM_L | ARM_U;                  // ┘
    case 0x251C: return ARM_U | ARM_D | ARM_R;          // ├
    case 0x2524: return ARM_U | ARM_D | ARM_L;          // ┤
    case 0x252C: return ARM_L | ARM_R | ARM_D;          // ┬
    case 0x2534: return ARM_L | ARM_R | ARM_U;          // ┴
    case 0x253C: return ARM_L | ARM_R | ARM_U | ARM_D;  // ┼
    case 0x2574: return ARM_L;                          // ╴
    case 0x2575: return ARM_U;                          // ╵
    case 0x2576: return ARM_R;                          // ╶
    case 0x2577: return ARM_D;                          // ╷
    default:     return 0;
    }
}

} // namespace

bool FontAtlas::hasDownArm(uint32_t cp) { return (boxArms(cp) & ARM_D) != 0; }

bool FontAtlas::drawBox(uint32_t cp, GlyphInfo& info) {
    const uint8_t arms = boxArms(cp);
    if (!arms) return false;
    // Drawn, not taken from the font: exactly one cell, whole pixels, full
    // coverage. The font's │ overhangs its cell above and below and its
    // stroke falls between pixels, so where two rows' overhangs met the
    // half-covered edge columns were painted twice — a faint band at every
    // joint of a vertical frame. Cut to the cell, nothing overlaps; the
    // stroke is the font's own weight (100 of 600 units) and sits where
    // the font's ─ does, like the slider's and charts' shapes.
    const int cw = cell_w_, ch = cell_h_;
    const BoxLine L = BoxLineIn(cw, ch);
    const int s = L.stroke, x0 = L.x0, y0 = L.y0;
    int ax = 0, ay = 0;
    if (!pack(cw, ch, ax, ay)) return false;
    auto fill = [&](int xa, int ya, int xb, int yb) {       // [xa, xb) × [ya, yb)
        for (int y = std::max(0, ya); y < std::min(ch, yb); ++y)
            for (int x = std::max(0, xa); x < std::min(cw, xb); ++x)
                pixels_[(size_t)(ay + y) * tex_w_ + ax + x] = 255;
    };
    if (arms & ARM_L) fill(0, y0, x0 + s, y0 + s);
    if (arms & ARM_R) fill(x0, y0, cw, y0 + s);
    if (arms & ARM_U) fill(x0, 0, x0 + s, y0 + s);
    if (arms & ARM_D) fill(x0, y0, x0 + s, ch);
    info.ax = ax; info.ay = ay; info.aw = cw; info.ah = ch;
    info.dx = 0; info.dy = 0;
    info.blank = false;
    dirty_ = true;
    return true;
}

const GlyphInfo& FontAtlas::glyph(uint32_t cp) {
    // Every non-blank cell of every frame asks for its glyph, so the common
    // codepoints skip the hash map: a direct table of pointers into it (the
    // map's nodes never move, so the pointers stay valid until init()).
    if (cp < fast_.size() && fast_[cp]) return *fast_[cp];
    auto it = cache_.find(cp);
    const GlyphInfo& g = it != cache_.end() ? it->second : (cache_[cp] = rasterize(cp));
    if (cp < fast_.size()) fast_[cp] = &g;
    return g;
}

GlyphInfo FontAtlas::rasterize(uint32_t cp) {
    GlyphInfo info{};
    if (drawBox(cp, info)) return info;
    stbtt_fontinfo* fi = fi_of(fontinfo_);
    const int gi = stbtt_FindGlyphIndex(fi, (int)cp);
    // Runtime fallback: show the missing-glyph box rather than nothing, so a
    // gap is visible instead of silently swallowed.
    if (gi == 0) return cp != 0x25A1 ? glyph(0x25A1) : info;

    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(fi, gi, sx_, sy_, 0.0f, baseline_f_, &x0, &y0, &x1, &y1);
    const int gw = x1 - x0, gh = y1 - y0;
    if (gw <= 0 || gh <= 0) return info;                 // space and friends

    int ax = 0, ay = 0;
    if (!pack(gw, gh, ax, ay)) {
        std::fprintf(stderr, "FontAtlas: page full, dropping U+%04X\n", cp);
        return info;
    }
    std::vector<uint8_t> tmp((size_t)gw * gh, 0);
    stbtt_MakeGlyphBitmapSubpixel(fi, tmp.data(), gw, gh, gw, sx_, sy_, 0.0f, baseline_f_, gi);
    for (int j = 0; j < gh; ++j)
        std::memcpy(&pixels_[(size_t)(ay + j) * tex_w_ + ax], &tmp[(size_t)j * gw], (size_t)gw);

    info.ax = ax; info.ay = ay; info.aw = gw; info.ah = gh;
    // The glyph's own bbox places it horizontally: the advance equals the cell
    // width, so no extra centring is needed or wanted.
    info.dx = x0;
    info.dy = baseline_i_ + y0;
    info.blank = false;
    dirty_ = true;
    return info;
}
