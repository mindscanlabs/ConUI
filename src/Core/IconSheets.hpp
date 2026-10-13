#pragma once
#include <string>
#include <vector>

class RasterStore;

// ---------------------------------------------------------------------------
// Icon strips from resources.dat (docs/DESIGN_SYSTEM.md §16).
//
// An icon set is a family of PNG strips, one per pixel size:
//
//     <prefix>_<N>.png    count·N × N — `count` square icons side by side
//
// usually white masks that the renderer tints with a theme slot. The caption
// boxes (`caption_`) and the playground's sidebar sections (`sections_`) are
// both loaded this way. An icon is always drawn at its strip's own size, on
// whole pixels, never scaled — so the set has one strip per size the app can
// ask for, and if the exact one is missing the NEAREST is used as it is.
// ---------------------------------------------------------------------------

struct IconStrip {
    std::vector<int> ids;   // raster ids, one per icon
    int px = 0;             // their size, pixels
    bool valid() const { return px > 0 && !ids.empty(); }
};

/**
 * The strip `<prefix>_<px>.png` (or the nearest size within ±16) split into
 * `count` icons and added to `store`. Loaded once per (store, prefix, size)
 * and kept; returns null if no strip of that family is in resources.dat.
 */
const IconStrip* LoadIconStrip(RasterStore& store, const std::string& prefix, int px, int count);
