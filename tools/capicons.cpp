// Generates the caption box icons (docs/DESIGN_SYSTEM.md §8.1) as PNG strips,
// one per pixel size, into src/assets/caption/:
//
//     caption_<S>.png   5·S × S, RGBA: close | collapse | zoom | fullscreen | filled
//
// They are MASKS — white where the icon is, transparent elsewhere. The
// renderer tints them with a theme slot at draw time, so one set serves every
// theme and every state (danger close, accent hover, filled when pressed).
// Each size is drawn for its own pixel grid and shown at exactly that size,
// so one-pixel strokes stay crisp; nothing is ever scaled. 8 px is the
// Tiny density at 100 %.
//
// Replace any strip with your own art of the same layout and the app picks it
// up on the next build (the PNGs are packed into resources.dat).
//
//     g++ -O2 -std=c++17 -o capicons tools/capicons.cpp && ./capicons src/assets/caption

#include "pngmask.hpp"

#include <cmath>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "src/assets/caption";
    int written = 0;
    for (int s = 8; s <= 32; ++s) {
        pngmask::Strip sh(s, 5);
        const int e = s - 1;
        const int in = 2;                                     // inner inset, px
        const int k = std::max(2, (int)std::lround(s * 0.34)); // corner mark arm
        const int m = std::max(3, (int)std::lround(s * 0.45)); // zoom's inner box
        for (int i = 0; i < 4; ++i) sh.frame(i, 0, 0, e, e);   // every box is a square
        // 0 close: the empty square, as in System 7
        // 1 collapse: a line across the middle
        sh.hline(1, in, e - in, e / 2);
        // 2 zoom: a smaller square in the top-left corner
        sh.frame(2, in, in, in + m - 1, in + m - 1);
        // 3 fullscreen: two opposed corner marks
        sh.hline(3, in, in + k - 1, in);             sh.vline(3, in, in, in + k - 1);
        sh.hline(3, e - in - k + 1, e - in, e - in); sh.vline(3, e - in, e - in - k + 1, e - in);
        // 4 filled: the pressed state of any box
        sh.rect(4, 0, 0, e, e);

        const std::string path = dir + "/caption_" + std::to_string(s) + ".png";
        if (!sh.save(path)) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
        ++written;
    }
    std::printf("wrote %d strips to %s\n", written, dir.c_str());
    return 0;
}
