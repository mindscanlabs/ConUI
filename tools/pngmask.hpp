#pragma once
// Shared by the icon generators in tools/: a dependency-free PNG writer and a
// strip of square MASK icons (white where the icon is, transparent elsewhere).
// The app tints masks with a theme slot when it draws them, so one file serves
// every theme and every state.
//
// The PNG is stored uncompressed inside zlib — every PNG reader accepts that,
// and at icon sizes it costs nothing.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pngmask {

inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c;
}

inline void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));  v.push_back((uint8_t)x);
}

inline void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
}

inline bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> raw;                       // filter byte 0 + row, per row
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + (size_t)y * w * 4, rgba.begin() + (size_t)(y + 1) * w * 4);
    }
    std::vector<uint8_t> z{ 0x78, 0x01 };          // zlib header, stored blocks
    for (size_t i = 0; ; ) {
        const size_t n = std::min<size_t>(65535, raw.size() - i);
        const bool last = i + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)n); z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)~n); z.push_back((uint8_t)(~n >> 8));
        z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
        i += n;
        if (last) break;
    }
    uint32_t a = 1, b = 0;                          // adler32
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);

    std::vector<uint8_t> png{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<uint8_t> ihdr;
    be32(ihdr, (uint32_t)w); be32(ihdr, (uint32_t)h);
    ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });     // 8-bit RGBA, no interlace
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

/** `count` square icons of `s` px side by side: the layout the app loads. */
struct Strip {
    int s, count;
    std::vector<uint8_t> px;
    Strip(int size, int n) : s(size), count(n), px((size_t)size * n * size * 4, 0) {}
    void on(int icon, int x, int y) {
        if (x < 0 || y < 0 || x >= s || y >= s) return;
        uint8_t* p = &px[((size_t)y * s * count + (size_t)icon * s + x) * 4];
        p[0] = p[1] = p[2] = p[3] = 255;
    }
    void rect(int i, int x0, int y0, int x1, int y1) {          // filled, inclusive
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) on(i, x, y);
    }
    void hline(int i, int x0, int x1, int y, int t = 1) { rect(i, x0, y, x1, y + t - 1); }
    void vline(int i, int x, int y0, int y1, int t = 1) { rect(i, x, y0, x + t - 1, y1); }
    void frame(int i, int x0, int y0, int x1, int y1, int t = 1) {
        hline(i, x0, x1, y0, t); hline(i, x0, x1, y1 - t + 1, t);
        vline(i, x0, y0, y1, t); vline(i, x1 - t + 1, y0, y1, t);
    }
    bool save(const std::string& path) const { return writePng(path, s * count, s, px); }
};

} // namespace pngmask
