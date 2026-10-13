#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <vector>

// The error glitch (§9) — the one cyberpunk liberty the system allows itself.
// A reaction to an event, not a standing effect: the moment an element goes
// into error, its rows jolt sideways by 1–2 cells and some of its glyphs turn
// to noise, for three short steps, then everything settles back.
//
// It works on the cells the widget has ALREADY drawn this frame, so any widget
// gets it by calling Glitch() after drawing itself. The steps are timed, not
// counted in frames, so the jolt looks the same at 30 fps and at 144.

namespace ui {

namespace {

struct GlitchState {
    bool   was = false;        // in error last frame
    double start = -1.0;       // when the current glitch began
};

constexpr double kDuration  = 0.21;  // three steps of 70 ms
constexpr int    kSteps     = 3;
constexpr double kLoopPause = 0.2;   // between replays in a showcase

// The noise: shades and blocks from the vocabulary (§4), nothing else.
const uint32_t kNoise[] = {
    Gly::SHADE_25, Gly::SHADE_50, Gly::SHADE_75, Gly::FULL, Gly::UPPER_HALF, Gly::LOWER_HALF,
};
constexpr int kNoiseN = (int)(sizeof kNoise / sizeof *kNoise);

} // namespace

bool Glitch(Context& ctx, Id owner, const Rect& r, bool error) {
    // Its own memory, next to the owner's: one id, one type (Context::blob).
    GlitchState& st = ctx.blob<GlitchState>(owner ^ 0x9E3779B97F4A7C15ull);
    const double now = ctx.input().time;
    if (error && !st.was) st.start = now;       // the event: error just began
    st.was = error;
    if (!ctx.glitches() || st.start < 0.0) return false;   // the user's switch
    double t = now - st.start;
    if (t < 0.0) return false;
    // Playing, or waiting to play again in a showcase: the noise steps on
    // frames, not on events, so ask for the next one.
    if (t < kDuration || (ctx.forced() && error)) ctx.animate();
    // In a showcase (the States screen) an element sits in error for good, so
    // the glitch would play once and never again. There it loops instead —
    // glitch, 0.2 s pause, glitch — so the effect can actually be seen.
    int cycle = 0;
    if (ctx.forced() && error) {
        const double period = kDuration + kLoopPause;
        cycle = (int)(t / period);
        t -= cycle * period;
    }
    if (t >= kDuration) return false;

    const int step = (int)(t / (kDuration / kSteps));
    const uint32_t seed = Mix32((uint32_t)owner ^ (uint32_t)(owner >> 32) ^
                              (uint32_t)(cycle * kSteps + step + 1) * 0x9E3779B9u);
    CellBuffer& cb = ctx.cells();
    std::vector<Cell> row((size_t)std::max(0, r.w));
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x)
            row[(size_t)(x - r.x)] = cb.in(x, y) ? cb.at(x, y) : Cell{};
        const uint32_t hy = Mix32(seed ^ (uint32_t)y * 0x85EBCA6Bu);
        // Every row jolts: 1 or 2 cells, left or right — never 0.
        const int dx = ((hy & 1u) ? 1 : -1) * (1 + (int)((hy >> 1) & 1u));
        for (int x = r.x; x < r.x + r.w; ++x) {
            const int sx = x - dx;
            const Cell& under = row[(size_t)(x - r.x)];
            Cell c = (sx >= r.x && sx < r.x + r.w) ? row[(size_t)(sx - r.x)]
                                                   : Cell{ ' ', under.fg, under.bg, ATTR_NONE };
            c.bg = under.bg;                    // the element keeps its shape; only ink moves
            const uint32_t hx = Mix32(hy ^ (uint32_t)x * 0xC2B2AE35u);
            if (c.cp != ' ' && c.cp != 0 && hx % 100 < 30) {
                c.cp = kNoise[(hx >> 8) % kNoiseN];
                c.fg = DANGER;
            }
            cb.put(x, y, c.cp, c.fg, c.bg, c.attr);
        }
    }
    return true;
}

} // namespace ui
