// Generates the playground's section icons for the sidebar (docs/DESIGN_SYSTEM.md
// §8.18) as PNG strips, one per pixel size, into src/assets/sections/:
//
//     sections_<S>.png   5·S × S, RGBA: workspace | states | glyphs | charts | diagrams
//
// White masks, tinted with a theme slot when drawn — the same scheme as the
// caption icons (tools/capicons.cpp). The sidebar draws an icon one row tall,
// so sizes cover every cell height the app can have: 12..48 px (Tiny at
// 100 % to Large at 200 %). Strokes thicken with the size so a 2× icon does
// not look like a wire drawing. Replace any strip with your own art.
//
//     g++ -O2 -std=c++17 -o secicons tools/secicons.cpp && ./secicons src/assets/sections

#include "pngmask.hpp"

#include <cmath>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "src/assets/sections";
    int written = 0;
    for (int s = 12; s <= 48; ++s) {
        pngmask::Strip sh(s, 5);
        const int t = std::max(1, (int)std::lround(s / 20.0));      // stroke
        const int m = std::max(1, (int)std::lround(s * 0.08));      // outer margin
        const int a = m, b = s - 1 - m;                              // the icon's box

        // 0 workspace: a window split into panels — a frame, a column seam,
        //   and the right-hand side split into two rows.
        sh.frame(0, a, a, b, b, t);
        const int vx = a + (int)std::lround((b - a) * 0.42);
        sh.vline(0, vx, a, b, t);
        const int hy = a + (int)std::lround((b - a) * 0.52);
        sh.hline(0, vx, b, hy, t);

        // 1 states: three rows, each a marker and a line — the first marker
        //   filled, the others hollow, like a list of states.
        const int bs = std::max(3, (int)std::lround(s * 0.2));      // marker size
        const int gap = std::max(2, (int)std::lround(s * 0.12));
        for (int k = 0; k < 3; ++k) {
            const int cy = a + (int)std::lround((b - a) * (k + 0.5) / 3.0);
            const int y0 = cy - bs / 2, y1 = y0 + bs - 1;
            if (k == 0) sh.rect(1, a, y0, a + bs - 1, y1);
            else        sh.frame(1, a, y0, a + bs - 1, y1, t);
            sh.hline(1, a + bs + gap, b, cy - t / 2, t);
        }

        // 2 glyphs: a character sheet — a 3×3 grid with one cell filled.
        sh.frame(2, a, a, b, b, t);
        const int c1 = a + (int)std::lround((b - a) / 3.0);
        const int c2 = a + (int)std::lround((b - a) * 2.0 / 3.0);
        sh.vline(2, c1, a, b, t); sh.vline(2, c2, a, b, t);
        sh.hline(2, a, b, c1, t); sh.hline(2, a, b, c2, t);
        sh.rect(2, a, a, c1, c1);

        // 3 charts: the two axes and three bars of rising height.
        sh.vline(3, a, a, b, t);
        sh.hline(3, a, b, b - t + 1, t);
        const int bw = std::max(2, (int)std::lround((b - a) * 0.18));
        const int span = b - t - a;                                  // room above the axis
        for (int k = 0; k < 3; ++k) {
            const int x0 = a + t + (int)std::lround((b - a - t) * (0.12 + k * 0.3));
            const int h = (int)std::lround(span * (0.35 + k * 0.3));
            sh.rect(3, x0, b - t - h, x0 + bw - 1, b - t);
        }

        // 4 diagrams: a tree — a filled box at the top, two hollow ones at
        //   the bottom corners, joined by a ┬ of lines.
        const int nb = std::max(4, (int)std::lround((b - a) * 0.36));   // box side
        const int rx0 = (a + b + 1) / 2 - nb / 2;
        sh.rect(4, rx0, a, rx0 + nb - 1, a + nb - 1);
        sh.frame(4, a, b - nb + 1, a + nb - 1, b, t);
        sh.frame(4, b - nb + 1, b - nb + 1, b, b, t);
        const int my = (a + nb + b - nb + 1) / 2;                        // the crossbar
        const int mx = rx0 + nb / 2 - t / 2;
        sh.vline(4, mx, a + nb, my, t);
        const int lx = a + nb / 2 - t / 2, rx = b - nb + 1 + nb / 2 - t / 2;   // the legs, centred on the boxes
        sh.hline(4, lx, rx + t - 1, my, t);
        sh.vline(4, lx, my, b - nb, t);
        sh.vline(4, rx, my, b - nb, t);

        const std::string path = dir + "/sections_" + std::to_string(s) + ".png";
        if (!sh.save(path)) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
        ++written;
    }
    std::printf("wrote %d strips to %s\n", written, dir.c_str());
    return 0;
}
