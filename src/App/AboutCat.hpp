#pragma once
#include "../UI/Context.hpp"

#include <cstdint>
#include <vector>

class RasterStore;
struct Theme;

// ---------------------------------------------------------------------------
// The ABOUT box's cat: a black cat with ring eyes, drawn in ASCII, turning
// in 3D to a three-quarter view one way and the other, and blinking now and
// then. The cat is a signed distance field — body, head, ears, tail — traced
// one ray per cell. A cell on the silhouette, or in front of a part well
// behind it (the tail across the body), becomes a line: / \ | _, picked by
// the direction of that edge. The eyes are rings stamped where their centres
// land, narrowed as the face turns; the coat is tabby stripes fixed to the
// surface; around the cat, a halftone of dots, as on the cassette sleeve.
// ---------------------------------------------------------------------------

namespace aboutcat {

/** What a cell shows; the caller maps it to a palette slot. */
enum Tone : uint8_t {
    TONE_DOTS,     // the halftone around the cat
    TONE_BODY,     // inside the cat: blank, or a dot where the rim catches light
    TONE_LINE,     // the outline
    TONE_EYE,      // the eyes' rings
    TONE_FUR,      // the coat inside the cat: stripes over a grain, fixed to its surface
};

/** Pix::flags. */
enum : uint8_t {
    PIX_SILHOUETTE = 1,   // a line on the cat's edge against the background, not inside it
};

/** Pix::dist units: world units times this. */
constexpr float DIST_SCALE = 4096.0f;

struct Pix {
    char    ch = ' ';
    uint8_t tone = TONE_DOTS;
    uint8_t flags = 0;
    // The signed distance from the cell's centre to the silhouette, in the
    // cat's world units times DIST_SCALE: negative on the cat, positive off
    // it. Sampled smoothly between cells, its zero is the edge — the layer
    // draws the outline there as a line instead of characters.
    int16_t dist = 0;
};

/**
 * One frame, `w` x `h` cells, row by row. `cellAspect` is the cell's width
 * over its height; `turn` the angle about the vertical axis, radians — 0
 * faces the viewer; `blink` 0 eyes open .. 1 shut.
 */
void Render(int w, int h, float cellAspect, float turn, std::vector<Pix>& out, float blink = 0.0f);

/** Render() in two parts: everything but the eyes, which depends on the turn alone ... */
void RenderBody(int w, int h, float cellAspect, float turn, std::vector<Pix>& out);
/** ... and the eyes, stamped over a body from RenderBody() at the same turn. */
void AddEyes(int w, int h, float cellAspect, float turn, float blink, std::vector<Pix>& out);

/** An eye as a shape, in grid coordinates (cell x spans x .. x+1). */
struct Eye {
    bool  visible = false;   // on the side facing the viewer and not behind the head
    float cx = 0, cy = 0;    // the centre
    float a = 0, b = 0;      // half-axes, open: across (narrowed as the face turns), down
    float open = 1;          // 1 open .. 0 shut
    float facing = 0;        // how squarely the eye faces the viewer, 0.2 .. 1
};

/** Both eyes at a turn and blink — what AddEyes() stamps, for drawing them smooth. */
void EyeShapes(int w, int h, float cellAspect, float turn, float blink, Eye out[2]);

/** The cat's world units per row of a `w` x `h` grid: Pix::dist to rows. */
float UnitsPerRow(int w, int h, float cellAspect);

/**
 * The shadow on the floor under the cat, an ellipse in grid coordinates
 * (cell x spans x .. x+1): its centre and half-axes.
 */
void Ground(int w, int h, float cellAspect, float& cx, float& cy, float& rx, float& ry);

} // namespace aboutcat

/**
 * The cat in `r` (absolute cells), turning to and fro with `time` (seconds);
 * a fixed three-quarter view if `frozen`. Asks for the next frame itself
 * while it turns.
 *
 * With `rasters` and `theme`, the cat is a layer of its own: a character
 * per `scale` of the cell (0.25 — four times the detail each way), set in
 * the UI font at four times that size, painted in the theme's colours,
 * averaged down 4 x 4 into one picture and shown as a sprite (§16), redrawn
 * in place each frame. There the outline is a smooth line along the
 * silhouette, and the cat casts a soft shadow. Without them, or at a scale
 * of 1 and up, it is drawn in the cells of `r`, one character per cell.
 */
void DrawAboutCat(ui::Context& ctx, RasterStore* rasters, const Theme* theme, const ui::Rect& r,
                  float scale, double time, bool frozen);
