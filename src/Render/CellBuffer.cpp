#include "CellBuffer.hpp"
#include "../UI/Theme.hpp"

#include <algorithm>

namespace {

/** Decodes the codepoint at `p` and steps past it. Invalid bytes become U+FFFD. */
uint32_t nextCodepoint(const unsigned char*& p) {
    uint32_t c = *p;
    int n = 0;
    if (c < 0x80)                { n = 0; }
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; n = 3; }
    else { ++p; return 0xFFFD; }
    ++p;
    for (int i = 0; i < n; ++i) {
        if ((*p & 0xC0) != 0x80) return 0xFFFD;             // truncated sequence
        c = (c << 6) | (*p & 0x3F);
        ++p;
    }
    return c;
}

} // namespace

std::vector<uint32_t> Utf8Decode(const char* s) {
    std::vector<uint32_t> out;
    for (const unsigned char* p = (const unsigned char*)s; p && *p;) out.push_back(nextCodepoint(p));
    return out;
}

// Counted and drawn straight from the bytes: both run for every label of
// every frame, and a vector per call was most of their cost.
int Utf8Width(const char* s) {
    int n = 0;
    for (const unsigned char* p = (const unsigned char*)s; p && *p; ++n) nextCodepoint(p);
    return n;
}

void Utf8Append(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

void CellBuffer::resize(int w, int h, uint8_t fill_bg) {
    w_ = w < 0 ? 0 : w;
    h_ = h < 0 ? 0 : h;
    cells_.assign((size_t)w_ * h_, Cell{ ' ', 8, fill_bg, ATTR_NONE });
    clearLayers();
}

void CellBuffer::clear(uint8_t fill_bg) {
    for (Cell& c : cells_) c = Cell{ ' ', 8, fill_bg, ATTR_NONE };
    clearLayers();
}

void CellBuffer::clearLayers() {
    // The overlay grid starts every frame empty: a cell nobody wrote in the
    // overlay band shows the content underneath.
    over_.assign(cells_.size(), Cell{ ' ', TEXT, BG_PANEL, ATTR_NONE });
    over_set_.assign(cells_.size(), 0);
    dim_base_ = false;
    surfaces_.clear();
    shapes_.clear();
    sprites_.clear();
    raises_.clear();
    band_ = Z_CONTENT;
    clip_ = ClipRect{};
    clip_stack_.clear();
}

void CellBuffer::pushClip(int x, int y, int w, int h) {
    clip_stack_.push_back(clip_);
    ClipRect c = clip_;
    if (x > c.x0) c.x0 = (int16_t)x;
    if (y > c.y0) c.y0 = (int16_t)y;
    if (x + w < c.x1) c.x1 = (int16_t)(x + w);
    if (y + h < c.y1) c.y1 = (int16_t)(y + h);
    if (c.x1 < c.x0) c.x1 = c.x0;       // empty, never inverted
    if (c.y1 < c.y0) c.y1 = c.y0;
    clip_ = c;
}

void CellBuffer::popClip() {
    if (clip_stack_.empty()) { clip_ = ClipRect{}; return; }
    clip_ = clip_stack_.back();
    clip_stack_.pop_back();
}

void CellBuffer::surface(int x, int y, int w, int h, uint8_t slot, uint8_t framed) {
    // Surfaces are whole cells, so they are simply cut to the clip here — and
    // an edge the clip cut no longer has its frame line to stop at.
    const int x0 = x > clip_.x0 ? x : clip_.x0, y0 = y > clip_.y0 ? y : clip_.y0;
    const int x1 = x + w < clip_.x1 ? x + w : clip_.x1;
    const int y1 = y + h < clip_.y1 ? y + h : clip_.y1;
    if (x0 != x) framed &= (uint8_t)~(EDGE_L | EDGE_INK_L);
    if (y0 != y) framed &= (uint8_t)~EDGE_T;
    if (x1 != x + w) framed &= (uint8_t)~(EDGE_R | EDGE_INK_R);
    if (y1 != y + h) framed &= (uint8_t)~EDGE_B;
    x = x0; y = y0; w = x1 - x0; h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    surfaces_.push_back(Surface{ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h,
                                 slot, (uint8_t)band_, framed });
}

void CellBuffer::shape(float x, float y, float w, float h, uint8_t slot,
                       float rfrac, float alpha, float stroke_px) {
    if (w <= 0.0f || h <= 0.0f) return;
    if (x >= clip_.x1 || y >= clip_.y1 || x + w <= clip_.x0 || y + h <= clip_.y0) return;
    shapes_.push_back(Shape{ x, y, w, h, rfrac, alpha, stroke_px, slot, (uint8_t)band_, clip_ });
}

void CellBuffer::segment(float x0, float y0, float x1, float y1, uint8_t slot, float thick_px,
                         float alpha) {
    if (thick_px <= 0.0f) return;
    if (std::min(x0, x1) >= clip_.x1 || std::min(y0, y1) >= clip_.y1 ||
        std::max(x0, x1) <= clip_.x0 || std::max(y0, y1) <= clip_.y0) return;
    Shape s{ x0, y0, x1 - x0, y1 - y0, 0.0f, alpha, thick_px, slot, (uint8_t)band_, clip_ };
    s.kind = SHAPE_SEGMENT;
    shapes_.push_back(s);
}

void CellBuffer::sprite(float x, float y, float w, float h, uint16_t id, float alpha,
                        uint8_t tint) {
    if (w <= 0.0f || h <= 0.0f) return;
    if (x >= clip_.x1 || y >= clip_.y1 || x + w <= clip_.x0 || y + h <= clip_.y0) return;
    sprites_.push_back(SpriteRef{ x, y, w, h, alpha, id, (uint8_t)band_, clip_, tint });
}

void CellBuffer::fill(int x, int y, int w, int h, uint32_t cp, uint8_t fg, uint8_t bg) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) put(i, j, cp, fg, bg);
}

void CellBuffer::bgRect(int x, int y, int w, int h, uint8_t bg) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) setBg(i, j, bg);
}

int CellBuffer::text(int x, int y, const char* s, uint8_t fg, uint8_t bg, uint8_t attr) {
    int n = 0;
    for (const unsigned char* p = (const unsigned char*)s; p && *p; ++n)
        put(x + n, y, nextCodepoint(p), fg, bg, attr);
    return n;
}

void CellBuffer::textRight(int xr, int y, const char* s, uint8_t fg, uint8_t bg, uint8_t attr) {
    text(xr - Utf8Width(s) + 1, y, s, fg, bg, attr);
}

std::string CellBuffer::dump() const {
    std::string out;
    out.reserve((size_t)w_ * h_ + h_);
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            uint32_t cp = top(x, y).cp;
            Utf8Append(out, cp == 0 ? ' ' : cp);
        }
        out += '\n';
    }
    return out;
}

Cell CellBuffer::top(int x, int y) const {
    const size_t i = (size_t)y * w_ + x;
    if (over_set_[i]) return over_[i];
    Cell c = cells_[i];
    // The solid translucent tint that does the actual dimming is a shape
    // drawn over the whole window (§8.9); here text just loses brightness,
    // same as any other dimmed cell, so it stays legible under the tint.
    if (dim_base_) c.attr |= ATTR_DIM;
    return c;
}

void CellBuffer::glitchRows(int y0, int y1, int dx, bool overlays) {
    y0 = std::max(0, y0);
    y1 = std::min(h_, y1);
    if (dx == 0 || y0 >= y1 || w_ <= 0) return;
    std::vector<Cell> row((size_t)w_);
    std::vector<uint8_t> set((size_t)w_);
    for (int y = y0; y < y1; ++y) {
        Cell* c = &cells_[(size_t)y * w_];
        std::copy(c, c + w_, row.begin());
        for (int x = 0; x < w_; ++x) {
            const int sx = x - dx;
            c[x] = (sx >= 0 && sx < w_) ? row[(size_t)sx] : Cell{};
        }
        if (!overlays) continue;
        Cell* o = &over_[(size_t)y * w_];
        uint8_t* os = &over_set_[(size_t)y * w_];
        std::copy(o, o + w_, row.begin());
        std::copy(os, os + w_, set.begin());
        for (int x = 0; x < w_; ++x) {
            const int sx = x - dx;
            const bool in = sx >= 0 && sx < w_;
            o[x] = in ? row[(size_t)sx] : Cell{};
            os[x] = in ? set[(size_t)sx] : 0;
        }
    }
    auto inRows = [&](float y, float h) { const float m = y + h * 0.5f; return m >= y0 && m < y1; };
    auto moveClip = [&](ClipRect& c) {
        if (c.x0 > -32768) c.x0 = (int16_t)(c.x0 + dx);
        if (c.x1 < 32767)  c.x1 = (int16_t)(c.x1 + dx);
    };
    for (Shape& s : shapes_)
        if ((s.z == Z_CONTENT || overlays) && inRows(s.y, s.h)) { s.x += dx; moveClip(s.clip); }
    for (SpriteRef& s : sprites_)
        if ((s.z == Z_CONTENT || overlays) && inRows(s.y, s.h)) { s.x += dx; moveClip(s.clip); }
}

void CellBuffer::pushClipReset(int x, int y, int w, int h) {
    clip_stack_.push_back(clip_);
    ClipRect c;
    c.x0 = (int16_t)x;
    c.y0 = (int16_t)y;
    c.x1 = (int16_t)(x + (w > 0 ? w : 0));
    c.y1 = (int16_t)(y + (h > 0 ? h : 0));
    clip_ = c;
}
