#include "RasterStore.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

#include <algorithm>
#include <cstring>

namespace {
const RasterInfo kNone{};
// One transparent texel of padding around every image, so bilinear sampling
// at the edge of one picture cannot bleed into its neighbour on the page.
const int kPad = 1;
} // namespace

bool RasterStore::init(int pageW, int pageH) {
    if (pageW <= 0 || pageH <= 0) return false;
    page_w_ = pageW;
    page_h_ = pageH;
    page_.assign((size_t)pageW * pageH * 4, 0);
    pen_x_ = pen_y_ = row_h_ = 0;
    items_.clear();
    dirty_ = dirty_all_ = true;
    return true;
}

bool RasterStore::place(int w, int h, int& outX, int& outY) {
    const int aw = w + kPad * 2, ah = h + kPad * 2;
    if (aw > page_w_ || ah > page_h_) return false;
    if (pen_x_ + aw > page_w_) {          // next shelf
        pen_x_ = 0;
        pen_y_ += row_h_;
        row_h_ = 0;
    }
    if (pen_y_ + ah > page_h_) return false;
    outX = pen_x_ + kPad;
    outY = pen_y_ + kPad;
    pen_x_ += aw;
    if (ah > row_h_) row_h_ = ah;
    return true;
}

int RasterStore::addRGBA(const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0 || page_.empty()) return -1;
    int x = 0, y = 0;
    if (!place(w, h, x, y)) return -1;

    for (int j = 0; j < h; ++j)
        std::memcpy(&page_[((size_t)(y + j) * page_w_ + x) * 4],
                    rgba + (size_t)j * w * 4, (size_t)w * 4);

    items_.push_back(RasterInfo{ x, y, w, h, true });
    dirty_ = dirty_all_ = true;
    return (int)items_.size() - 1;
}

bool RasterStore::update(int id, const uint8_t* rgba) {
    if (!rgba || id < 0 || id >= (int)items_.size() || !items_[(size_t)id].valid) return false;
    const RasterInfo& r = items_[(size_t)id];
    for (int j = 0; j < r.ah; ++j)
        std::memcpy(&page_[((size_t)(r.ay + j) * page_w_ + r.ax) * 4],
                    rgba + (size_t)j * r.aw * 4, (size_t)r.aw * 4);
    if (!dirty_) {
        dx0_ = r.ax; dy0_ = r.ay; dx1_ = r.ax + r.aw; dy1_ = r.ay + r.ah;
    } else if (!dirty_all_) {
        dx0_ = std::min(dx0_, r.ax); dy0_ = std::min(dy0_, r.ay);
        dx1_ = std::max(dx1_, r.ax + r.aw); dy1_ = std::max(dy1_, r.ay + r.ah);
    }
    dirty_ = true;
    return true;
}

int RasterStore::addFromMemory(const uint8_t* bytes, size_t n) {
    if (!bytes || !n) return -1;
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(bytes, (int)n, &w, &h, &comp, 4);
    if (!px) return -1;
    const int id = addRGBA(px, w, h);
    stbi_image_free(px);
    return id;
}

int RasterStore::addStripFromMemory(const uint8_t* bytes, size_t size, int n, int* outIds) {
    if (!bytes || !size || n <= 0 || !outIds) return 0;
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(bytes, (int)size, &w, &h, &comp, 4);
    if (!px) return 0;
    const int cw = w / n;
    int ok = cw > 0 && cw * n == w ? cw : 0;
    std::vector<uint8_t> cell((size_t)cw * h * 4);
    for (int i = 0; ok && i < n; ++i) {
        for (int y = 0; y < h; ++y)
            std::memcpy(&cell[(size_t)y * cw * 4], px + ((size_t)y * w + (size_t)i * cw) * 4,
                        (size_t)cw * 4);
        outIds[i] = addRGBA(cell.data(), cw, h);
        if (outIds[i] < 0) ok = 0;
    }
    stbi_image_free(px);
    return ok;
}

const RasterInfo& RasterStore::get(uint16_t id) const {
    return id < items_.size() ? items_[id] : kNone;
}
