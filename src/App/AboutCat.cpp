#include "AboutCat.hpp"
#include "../Core/RasterStore.hpp"
#include "../Core/ResourceManager.hpp"
#include "../Render/CellBuffer.hpp"
#include "../Render/FontAtlas.hpp"
#include "../UI/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace aboutcat {
namespace {

struct V3 { float x, y, z; };
V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 operator*(V3 a, float k) { return { a.x * k, a.y * k, a.z * k }; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Len(V3 a) { return std::sqrt(Dot(a, a)); }
V3 Norm(V3 a) { const float l = Len(a); return l > 0.0f ? a * (1.0f / l) : a; }

// The distance functions (Quilez). The ellipsoid's is a bound, not exact;
// the march below steps short to make up for it.
float SdEllipsoid(V3 p, V3 r) {
    const V3 q{ p.x / r.x, p.y / r.y, p.z / r.z };
    const V3 q2{ q.x / r.x, q.y / r.y, q.z / r.z };
    const float k0 = Len(q), k1 = Len(q2);
    return k1 > 0.0f ? k0 * (k0 - 1.0f) / k1 : -std::min(r.x, std::min(r.y, r.z));
}
float SdCapsule(V3 p, V3 a, V3 b, float r) {
    const V3 pa = p - a, ba = b - a;
    const float h = std::clamp(Dot(pa, ba) / Dot(ba, ba), 0.0f, 1.0f);
    return Len(pa - ba * h) - r;
}
// A cone with round ends: radius r1 at a, r2 at b.
float SdRoundCone(V3 p, V3 a, V3 b, float r1, float r2) {
    const V3 ba = b - a, pa = p - a;
    const float l2 = Dot(ba, ba), rr = r1 - r2, a2 = l2 - rr * rr, il2 = 1.0f / l2;
    const float y = Dot(pa, ba), z = y - l2;
    const V3 w = pa * l2 - ba * y;
    const float x2 = Dot(w, w), y2 = y * y * l2, z2 = z * z * l2;
    const float k = (rr > 0.0f ? 1.0f : -1.0f) * rr * rr * x2;
    if ((z > 0.0f ? 1.0f : -1.0f) * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - r2;
    if ((y > 0.0f ? 1.0f : -1.0f) * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - r1;
    return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}
float Smin(float a, float b, float k) {
    const float h = std::max(k - std::fabs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

// The cat in its own space: y up, z toward the viewer, the ear tips at
// y ≈ 1 and the bottom at y ≈ -0.98. A pear with no neck, as on the sleeve.
constexpr V3    HEAD_C{ 0.0f, 0.28f, 0.02f };
constexpr V3    HEAD_R{ 0.58f, 0.44f, 0.44f };
float CatSdf(V3 p) {
    const float body = SdEllipsoid(p - V3{ 0.0f, -0.42f, -0.02f }, { 0.66f, 0.56f, 0.48f });
    const float head = SdEllipsoid(p - HEAD_C, HEAD_R);
    float d = Smin(body, head, 0.35f);
    // The ears: flat round cones, one mirrored into the other.
    const V3 q{ std::fabs(p.x), p.y, p.z * 1.7f };
    d = Smin(d, 0.7f * SdRoundCone(q, { 0.30f, 0.50f, 0.0f }, { 0.48f, 1.02f, 0.0f }, 0.22f, 0.03f),
             0.08f);
    // The tail: out of the back at the bottom, round to the left and up.
    static const V3 kTail[] = { { -0.30f, -0.88f, -0.30f }, { -0.68f, -0.82f, -0.28f },
                                { -0.88f, -0.58f, -0.24f }, { -0.92f, -0.25f, -0.20f },
                                { -0.86f, 0.02f, -0.16f },  { -0.74f, 0.16f, -0.12f } };
    // The tail is the costliest part: skip it where a sphere round it is
    // already farther than the blend could reach.
    if (Len(p - V3{ -0.62f, -0.36f, -0.21f }) - 0.72f > d + 0.06f) return d;
    float tail = 1e9f;
    for (int i = 0; i + 1 < 6; ++i) tail = std::min(tail, SdCapsule(p, kTail[i], kTail[i + 1], 0.09f));
    return Smin(d, tail, 0.06f);
}

V3 CatNormal(V3 p) {
    // The tetrahedron of samples: four distance calls instead of six.
    const float e = 0.004f;
    const V3 k0{ 1, -1, -1 }, k1{ -1, -1, 1 }, k2{ -1, 1, -1 }, k3{ 1, 1, 1 };
    return Norm(k0 * CatSdf(p + k0 * e) + k1 * CatSdf(p + k1 * e) +
                k2 * CatSdf(p + k2 * e) + k3 * CatSdf(p + k3 * e));
}

// Smooth value noise in 0..1: a hashed lattice, blended trilinearly.
float Lattice(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)y * 0xD8163841u ^ (uint32_t)z * 0xCB1AB31Fu;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return (float)(h & 0xFFFFFF) / 16777216.0f;
}
float ValueNoise(V3 p) {
    const float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
    const int x = (int)fx, y = (int)fy, z = (int)fz;
    auto s = [](float t) { return t * t * (3.0f - 2.0f * t); };
    const float u = s(p.x - fx), v = s(p.y - fy), w = s(p.z - fz);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    return lerp(lerp(lerp(Lattice(x, y, z), Lattice(x + 1, y, z), u),
                     lerp(Lattice(x, y + 1, z), Lattice(x + 1, y + 1, z), u), v),
                lerp(lerp(Lattice(x, y, z + 1), Lattice(x + 1, y, z + 1), u),
                     lerp(Lattice(x, y + 1, z + 1), Lattice(x + 1, y + 1, z + 1), u), v), w);
}

// The coat at p (object space), 0 bare .. 1 dense: tabby stripes round the
// body, bent by a slow noise, over a fine grain. Fixed to the cat's surface,
// so it turns with it — the best cue there is that the cat is round.
float Fur(V3 p) {
    const float bend = ValueNoise(p * 3.0f);
    const float band = 0.5f + 0.5f * std::sin(p.y * 16.0f + p.x * 2.0f + 5.0f * bend);
    const float grain = ValueNoise(p * 24.0f);
    return std::clamp((band - 0.5f) * 2.0f, 0.0f, 1.0f) * 0.7f + grain * 0.4f;
}

// The eyes: rings on the head's surface, at (±EYE_X, EYE_Y), radius EYE_R.
constexpr float EYE_X = 0.24f, EYE_Y = 0.30f, EYE_R = 0.13f;

// How a grid of `w` x `h` cells sees the cat at a turn: the scale, the
// turn and the tilt, and the ray march.
struct View {
    int   w, h;
    float rowU, colU;
    float yMid = 0.02f;                                 // the ears reach higher than the bottom goes
    float ca, sa, ct, st;
    V3    rd;

    View(int w_, int h_, float cellAspect, float turn) : w(w_), h(h_) {
        // Fit about 2.3 units tall, and the tail's sweep (radius ~1.05) across.
        rowU = std::max(2.3f / (float)h, 2.25f / ((float)w * cellAspect));
        colU = rowU * cellAspect;
        const float tilt = 0.20f;                       // looking a little down at it
        ca = std::cos(turn); sa = std::sin(turn);
        ct = std::cos(tilt); st = std::sin(tilt);
        rd = toObj({ 0.0f, 0.0f, -1.0f });
    }
    V3 toObj(V3 v) const {                              // undo the tilt, then the turn
        const V3 a{ v.x, v.y * ct + v.z * st, -v.y * st + v.z * ct };
        return V3{ a.x * ca - a.z * sa, a.y, a.x * sa + a.z * ca };
    }
    V3 toWorld(V3 v) const {                            // the turn, then the tilt
        const V3 a{ v.x * ca + v.z * sa, v.y, -v.x * sa + v.z * ca };
        return V3{ a.x, a.y * ct - a.z * st, a.y * st + a.z * ct };
    }
    // The world point at the centre of cell (x, y), and back: the cell
    // whose centre a world point lands on, fractional.
    float worldX(int x) const { return ((float)x + 0.5f - w * 0.5f) * colU; }
    float worldY(int y) const { return (h * 0.5f - (float)y - 0.5f) * rowU + yMid; }
    float cellX(float wx) const { return wx / colU + w * 0.5f - 0.5f; }
    float cellY(float wy) const { return h * 0.5f - (wy - yMid) / rowU - 0.5f; }

    // The first surface along the view ray through world (wx, wy), in object
    // space. A ray that misses reports how near it came: for a ray grazing
    // the cat that is its distance from the silhouette.
    bool trace(float wx, float wy, V3& hitAt, float& nearest) const {
        nearest = 1.0f;
        if (wx * wx + wy * wy > 1.3f * 1.3f) return false;   // outside every turn
        const V3 ro = toObj({ wx, wy, 2.0f });
        // March only across the sphere that holds the cat in every turn.
        const float b = Dot(ro, rd), disc = b * b - (Dot(ro, ro) - 1.3f * 1.3f);
        if (disc <= 0.0f) return false;
        const float tEnd = -b + std::sqrt(disc);
        float t = std::max(0.0f, -b - std::sqrt(disc));
        for (int k = 0; k < 64 && t < tEnd; ++k) {
            const float d = CatSdf(ro + rd * t);
            nearest = std::min(nearest, d);
            if (d < 0.003f) { hitAt = ro + rd * t; return true; }
            t += d * 0.85f;
        }
        return false;
    }
};

} // namespace

float UnitsPerRow(int w, int h, float cellAspect) {
    return View(std::max(1, w), std::max(1, h), cellAspect, 0.0f).rowU;
}

void Ground(int w, int h, float cellAspect, float& cx, float& cy, float& rx, float& ry) {
    const View v(std::max(1, w), std::max(1, h), cellAspect, 0.0f);
    // Under the middle of the bottom, a little to the right of it: the
    // light comes from the upper left.
    const V3 g = v.toWorld({ 0.0f, -0.97f, 0.0f });
    cx = v.cellX(g.x + 0.12f) + 0.5f;
    cy = v.cellY(g.y) + 0.5f;
    rx = 0.95f / v.colU;
    ry = 0.13f / v.rowU;
}

void RenderBody(int w, int h, float cellAspect, float turn, std::vector<Pix>& out) {
    out.assign((size_t)std::max(0, w * h), Pix{});
    if (w <= 0 || h <= 0) return;
    const View v(w, h, cellAspect, turn);
    const V3 light = Norm(V3{ -0.6f, 0.6f, 0.5f });    // world: upper left, in front

    // One ray per cell: whether it hits, how near, the rim light, the coat;
    // or, missing, how near it passed.
    struct Hit { bool in = false; float z = 0, shade = 0, fur = 0, nearest = 1.0f; };
    std::vector<Hit> hits((size_t)w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            V3 p;
            Hit& c = hits[(size_t)y * w + x];
            if (!v.trace(v.worldX(x), v.worldY(y), p, c.nearest)) continue;
            const V3 n = v.toWorld(CatNormal(p));
            c.in = true;
            c.z = v.toWorld(p).z;
            const float rim = 1.0f - std::max(0.0f, n.z);
            c.shade = rim * rim * std::max(0.0f, Dot(n, light) + 0.3f);
            c.fur = Fur(p);
        }
    auto inside = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && hits[(size_t)y * w + x].in;
    };
    // Whether (x, y) lies beyond the surface seen at c: off the cat, or on a
    // part of it well behind — the tail passing in front of the body has an
    // outline too.
    auto beyond = [&](const Hit& c, int x, int y) {
        return !inside(x, y) || hits[(size_t)y * w + x].z < c.z - 0.25f;
    };

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * w + x;
            const Hit& c = hits[i];
            Pix& px = out[i];
            if (!c.in) {
                // A halftone: a dot every fourth cell, rows staggered, denser
                // toward the bottom as on the sleeve.
                const int ph = (x + 2 * y) & 3;
                px = Pix{ ph == 0 ? '.' : (ph == 2 && y * 2 >= h) ? ':' : ' ', TONE_DOTS };
                continue;
            }
            if (beyond(c, x - 1, y) || beyond(c, x + 1, y) || beyond(c, x, y - 1) || beyond(c, x, y + 1)) {
                const uint8_t sil = (!inside(x - 1, y) || !inside(x + 1, y) ||
                                     !inside(x, y - 1) || !inside(x, y + 1)) ? PIX_SILHOUETTE : 0;
                // The outline, along the edge: its direction is the Sobel
                // gradient of "beyond" around the cell, in square units.
                float gx = 0.0f, gy = 0.0f;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!beyond(c, x + dx, y + dy)) continue;
                        const float k = (dx == 0 || dy == 0) ? 2.0f : 1.0f;
                        gx += dx * k;
                        gy -= dy * k;                   // y up
                    }
                gx /= cellAspect;
                // A '/' spans its cell corner to corner, about 65° on a
                // 9 x 20 cell: steeper edges are '|', flatter '_'.
                const float phi = std::atan2(std::fabs(gy), std::fabs(gx)) * 57.29578f;
                if (phi < 22.0f) {
                    px = Pix{ '|', TONE_LINE, sil };
                } else if (phi < 48.0f) {
                    px = Pix{ (gx > 0.0f) == (gy > 0.0f) ? '\\' : '/', TONE_LINE, sil };
                } else if (gy > 0.0f && y > 0 && beyond(c, x, y - 1) &&
                           out[i - (size_t)w].tone != TONE_LINE) {
                    // A top edge lies on the cell's top, where '_' cannot
                    // reach: it goes into the cell above instead.
                    out[i - (size_t)w] = Pix{ '_', TONE_LINE, sil };
                    px = Pix{ ' ', TONE_BODY };
                } else {
                    px = Pix{ '_', TONE_LINE, sil };
                }
                continue;
            }
            // The rim light where it catches; the coat elsewhere, its density
            // spelled by how much ink a character has.
            if (c.shade > 0.16f) {
                px = Pix{ c.shade > 0.40f ? ':' : '.', TONE_BODY };
            } else {
                static const char kCoat[] = " .,:;=";
                const int k = std::clamp((int)((c.fur - 0.3f) * 7.0f), 0, 5);
                px = Pix{ kCoat[k], TONE_FUR };
            }
        }

    // The distance to the silhouette. Off the cat it is how near the ray
    // passed. On it, it is read off the neighbours that miss: the edge lies
    // between, as far from the miss as that ray passed from the cat.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Hit& c = hits[(size_t)y * w + x];
            float d;
            if (!c.in) {
                d = c.nearest;
            } else {
                d = -1.0f;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if ((dx == 0 && dy == 0) || inside(x + dx, y + dy)) continue;
                        if (x + dx < 0 || y + dy < 0 || x + dx >= w || y + dy >= h) continue;
                        const float gap = std::sqrt((dx * v.colU) * (dx * v.colU) + (dy * v.rowU) * (dy * v.rowU));
                        d = std::max(d, hits[(size_t)(y + dy) * w + x + dx].nearest - gap);
                    }
                d = std::min(d, -0.0005f);
            }
            out[(size_t)y * w + x].dist = (int16_t)std::clamp(d * DIST_SCALE, -32767.0f, 32767.0f);
        }
}

void EyeShapes(int w, int h, float cellAspect, float turn, float blink, Eye out[2]) {
    const View v(std::max(1, w), std::max(1, h), cellAspect, turn);
    const float open = 1.0f - std::clamp(blink, 0.0f, 1.0f);
    for (int k = 0; k < 2; ++k) {
        Eye& eye = out[k];
        eye = Eye{};
        const float ex = (k == 0 ? -1.0f : 1.0f) * EYE_X, ey = EYE_Y;
        const float u = ex / HEAD_R.x, vv = (ey - HEAD_C.y) / HEAD_R.y;
        const V3 e{ ex, ey, HEAD_C.z + HEAD_R.z * std::sqrt(std::max(0.0f, 1.0f - u * u - vv * vv)) };
        const V3 n = v.toWorld(CatNormal(e));
        if (n.z < 0.2f) continue;                       // edge-on or facing away
        const V3 ew = v.toWorld(e);
        V3 p;
        float nearest;
        if (!v.trace(ew.x, ew.y, p, nearest) || Len(p - e) > 0.05f) continue;   // behind the head
        eye.visible = true;
        eye.cx = v.cellX(ew.x) + 0.5f;
        eye.cy = v.cellY(ew.y) + 0.5f;
        eye.a = EYE_R * n.z / v.colU;                   // foreshortened as the face turns
        eye.b = EYE_R / v.rowU;
        eye.open = open;
        eye.facing = n.z;
    }
}

void AddEyes(int w, int h, float cellAspect, float turn, float blink, std::vector<Pix>& out) {
    if (w <= 0 || h <= 0 || out.size() != (size_t)w * h) return;
    auto inside = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && out[(size_t)y * w + x].dist < 0;
    };
    Eye eyes[2];
    EyeShapes(w, h, cellAspect, turn, blink, eyes);

    // The eyes, each a ring drawn round where its centre lands — large, an
    // ellipse with sloping shoulders; small, a box with round sides:
    //      .------.       .--.
    //     /        \     (    )
    //    (          )     `--'
    //     \        /
    //      `------'
    // narrowed as the face turns away, and gone once the head hides it.
    // Blinking, the ring flattens to a box, then shuts to a line.
    for (const Eye& eye : eyes) {
        if (!eye.visible) continue;
        const float fx = eye.cx - 0.5f, fy = eye.cy - 0.5f;   // cell indices
        const float a = eye.a, b = eye.b * std::max(eye.open, 0.05f);
        const bool shut = eye.open < 0.3f;
        int yt = (int)std::lround(fy - b * 0.85f), yb = (int)std::lround(fy + b * 0.85f);
        if (shut) {
            yt = yb = (int)std::lround(fy + eye.b * 0.15f);   // the lid, a little low
        } else if (yb - yt < 2) {
            yt = (int)std::lround(fy) - 1;
            yb = yt + 2;
        }
        // The ring's ends on row y: the ellipse's width there, measured on
        // the row's edge nearer the middle so the top and bottom rows span.
        // Three rows are too few for a curve: those eyes are boxes.
        const bool box = shut || yb - yt <= 2;
        auto ends = [&](int y, int& xl, int& xr) {
            const float yy = y < fy ? (float)y + 0.5f : y > fy ? (float)y - 0.5f : (float)y;
            const float t = box ? 0.0f : std::clamp((yy - fy) / b, -1.0f, 1.0f);
            const float hw = a * std::sqrt(1.0f - t * t);
            xl = (int)std::lround(fx - hw);
            xr = (int)std::lround(fx + hw);
        };
        // The silhouette wins over the ring where they meet; lines inside
        // it — an ear's base, the tail in front — give way. An eye mostly
        // cut is turning round the head's edge: it goes whole rather than
        // leave a stray bracket.
        auto blocked = [&](int x, int y) {
            if (!inside(x, y)) return true;
            return (out[(size_t)y * w + x].flags & PIX_SILHOUETTE) != 0;
        };
        int nBlocked = 0, nCells = 0;
        for (int y = yt; y <= yb; ++y) {
            int xl, xr;
            ends(y, xl, xr);
            for (int x = xl; x <= xr; ++x) { nBlocked += blocked(x, y); ++nCells; }
        }
        if (nBlocked * 3 > nCells) continue;
        auto put = [&](int x, int y, char ch, uint8_t tone) {
            if (blocked(x, y)) return;
            Pix& px = out[(size_t)y * w + x];
            px.ch = ch;
            px.tone = tone;
            px.flags = 0;
        };
        if (shut) {
            int xl, xr;
            ends(yt, xl, xr);
            for (int x = xl; x <= xr; ++x) put(x, yt, '-', TONE_EYE);
            continue;
        }
        for (int y = yt; y <= yb; ++y) {
            int xl, xr;
            ends(y, xl, xr);
            if (xr - xl <= 1) {                         // nearly edge-on: one column
                put(xl, y, y == yt ? '.' : y == yb ? '\'' : '|', TONE_EYE);
                continue;
            }
            const bool top = y == yt, bottom = y == yb;
            for (int x = xl + 1; x < xr; ++x)           // the pupil: as black as the cat
                put(x, y, top || bottom ? '-' : ' ', top || bottom ? TONE_EYE : TONE_BODY);
            if (top)    { put(xl, y, '.', TONE_EYE); put(xr, y, '.', TONE_EYE); continue; }
            if (bottom) { put(xl, y, '`', TONE_EYE); put(xr, y, '\'', TONE_EYE); continue; }
            // A side that moves two cells or more from the row nearer the
            // rim is a shoulder: / \ rather than ( ).
            int ol, orr;
            ends(y < fy ? y - 1 : y + 1, ol, orr);
            const bool shoulder = ol - xl >= 2;
            const bool upper = y < fy;
            put(xl, y, shoulder ? (upper ? '/' : '\\') : '(', TONE_EYE);
            put(xr, y, shoulder ? (upper ? '\\' : '/') : ')', TONE_EYE);
        }
    }
}

void Render(int w, int h, float cellAspect, float turn, std::vector<Pix>& out, float blink) {
    RenderBody(w, h, cellAspect, turn, out);
    AddEyes(w, h, cellAspect, turn, blink, out);
}

} // namespace aboutcat

namespace {

// The palette slot of each tone: the halftone in the dim accent — the teal
// of the sleeve in the dark themes — the rim light and the coat dim, the
// lines and the eyes bright. In the layer each is also laid on at its own
// strength (kToneAlpha, of 255): the halftone and the coat stay under the
// cat, so a black cat stays black under its stripes.
const uint8_t kToneSlot[]  = { ACCENT_DIM, TEXT_MUTED, TEXT, TEXT, TEXT_MUTED };
const uint8_t kToneAlpha[] = { 120, 255, 255, 255, 150 };

// The swing, sampled: thirty frames a second over a six-second period. The
// body is a function of the phase alone, so a phase once drawn is kept.
constexpr int    kFps = 30;
constexpr int    kPhases = 6 * kFps;
constexpr double kTwoPi = 6.283185307179586;

// From facing the viewer round to a three-quarter view on one side, back, and round
// to the other — 45° each way, slowing at the ends.
float TurnAt(int phase) {
    return (float)(0.7853982 * std::sin(phase * (kTwoPi / kPhases)));
}

// How shut the eyes are at `time`, 0..1: a blink of a fifth of a second
// every 4.7 seconds — not a divisor of the swing, so it falls on a new turn
// each time — and every third time two in a row.
float BlinkAt(double time) {
    const double period = 4.7, len = 0.2;
    const double k = std::floor(time / period), t = time - k * period;
    auto one = [&](double from) {
        const double u = (t - from) / len;
        return (u <= 0.0 || u >= 1.0) ? 0.0f : (float)(1.0 - std::fabs(u * 2.0 - 1.0));
    };
    const int n = (int)std::fmod(k, 3.0);
    return std::max(one(0.0), n == 2 ? one(0.32) : 0.0f);
}

// The cat in cells, one character per cell: the fallback when the layer
// cannot be drawn.
void DrawInCells(ui::Context& ctx, const ui::Rect& r, float aspect, float turn, float blink) {
    std::vector<aboutcat::Pix> pix;
    aboutcat::Render(r.w, r.h, aspect, turn, pix, blink);
    CellBuffer& cb = ctx.cells();
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            const aboutcat::Pix& p = pix[(size_t)y * r.w + x];
            if (p.ch == ' ') continue;
            const uint8_t attr = p.tone == aboutcat::TONE_EYE  ? ATTR_BOLD
                               : p.tone == aboutcat::TONE_DOTS ? ATTR_DIM : ATTR_NONE;
            cb.put(r.x + x, r.y + y, (uint32_t)(unsigned char)p.ch, kToneSlot[p.tone], BG_PANEL, attr);
        }
}

// The layer: the ASCII cat set in the UI font at SS times the size it is
// shown — at a quarter of the cell that is the UI's own cell, where the font
// is drawn best — and averaged down SS x SS, sixteen samples to a pixel, so
// the tiny characters keep their shapes as soft strokes. Between two phases
// of the swing the picture is a blend of both, so the characters along the
// edge fade from one to the next instead of flipping. Under it all a soft
// shadow; over it the outline, drawn as a smooth line where the distance to
// the silhouette is zero rather than in characters. One picture, redrawn in
// place every frame; a new one only when its size changes.
constexpr int SS = 4;
struct Layer {
    const RasterStore* store = nullptr;
    int id = -1, w = 0, h = 0;
    FontAtlas font;
    int fontW = 0, fontH = 0;
    bool fontOk = false;
    int gw = 0, gh = 0;                                 // the grid the phases below belong to
    std::vector<std::vector<aboutcat::Pix>> phases;     // the body, per phase, as traced
    std::vector<aboutcat::Pix> frame[2];                // this frame's phases, eyes added
    std::vector<uint32_t> acc;                          // per pixel: alpha, then r, g, b by alpha
    std::vector<uint8_t> rgba;
};
Layer g_layer;

// `phase` -1 is the frozen view at `turn`; otherwise phases `phase` and the
// next are blended, the next weighing `mix` (0..1).
bool DrawLayer(ui::Context& ctx, RasterStore& rasters, const Theme& theme, const ui::Rect& r,
               float scale, int phase, float mix, float turn, float blink) {
    const ui::Input& in = ctx.input();
    const int cw = in.cell_w, ch = in.cell_h;
    // The small cell as painted (SS times its size on screen), in whole
    // pixels so every glyph lands on the grid.
    const int mw = std::max(3, (int)std::lround(cw * scale * SS));
    const int mh = std::max(6, (int)std::lround(ch * scale * SS));
    const int gw = r.w * cw * SS / mw, gh = r.h * ch * SS / mh;
    if (gw < 8 || gh < 4) return false;

    Layer& L = g_layer;
    if (L.fontW != mw || L.fontH != mh) {
        size_t n = 0;
        const void* ttf = ResourceManager::Get().GetResource("JetBrainsMono-Regular.ttf", n);
        L.font = FontAtlas();
        L.fontOk = ttf && n &&
                   L.font.init(std::vector<uint8_t>((const uint8_t*)ttf, (const uint8_t*)ttf + n), mw, mh);
        L.fontW = mw;
        L.fontH = mh;
    }
    if (!L.fontOk) return false;

    // The body: traced once per phase and grid, then kept — after the first
    // swing a frame costs only the eyes and the painting.
    if (L.gw != gw || L.gh != gh) {
        L.phases.assign(kPhases, {});
        L.gw = gw;
        L.gh = gh;
    }
    const float aspect = (float)mw / (float)mh;
    // This frame's one or two phases, eyes and all, and their weights.
    int nFrames = 0;
    uint32_t weight[2] = { 256, 0 };
    auto take = [&](int ph, float t) {
        std::vector<aboutcat::Pix>& out = L.frame[nFrames++];
        if (ph < 0) {
            aboutcat::RenderBody(gw, gh, aspect, t, out);
        } else {
            std::vector<aboutcat::Pix>& body = L.phases[(size_t)ph];
            if (body.empty()) aboutcat::RenderBody(gw, gh, aspect, t, body);
            out = body;
        }
        aboutcat::AddEyes(gw, gh, aspect, t, blink, out);
    };
    if (phase < 0) {
        take(-1, turn);
    } else {
        const uint32_t wb = (uint32_t)std::lround(std::clamp(mix, 0.0f, 1.0f) * 256.0f);
        take(phase, TurnAt(phase));
        weight[0] = 256 - wb;
        if (wb > 0) {
            take((phase + 1) % kPhases, TurnAt((phase + 1) % kPhases));
            weight[1] = wb;
        }
    }

    // The characters, painted straight into the averages: every glyph texel
    // adds its coverage, by the phase's weight, to the screen pixel it
    // falls in. The silhouette's characters are left out: the line below
    // draws that edge.
    const int W = gw * mw / SS, H = gh * mh / SS;
    L.acc.assign((size_t)W * H * 4, 0);
    const uint8_t* atlas = L.font.pixels();
    const int aw = L.font.texW();
    auto stamp = [&](int cx, int cy, uint32_t cp, const Rgba& c, int shift, uint32_t wgt) {
        const GlyphInfo& g = L.font.glyph(cp);
        if (g.blank) return;
        for (int j = 0; j < g.ah; ++j) {
            const int y = cy * mh + g.dy + j;
            if (y < 0 || y >= H * SS) continue;
            uint32_t* row = &L.acc[(size_t)(y / SS) * W * 4];
            for (int i = 0; i < g.aw; ++i) {
                const int x = cx * mw + g.dx + i + shift;
                if (x < 0 || x >= W * SS) continue;
                const uint32_t a = (uint32_t)atlas[(size_t)(g.ay + j) * aw + g.ax + i] * c.a / 255 * wgt;
                if (!a) continue;
                uint32_t* d = row + (size_t)(x / SS) * 4;
                d[0] += a; d[1] += a / 255 * c.r; d[2] += a / 255 * c.g; d[3] += a / 255 * c.b;
            }
        }
    };
    for (int f = 0; f < nFrames; ++f) {
        const std::vector<aboutcat::Pix>& pix = L.frame[f];
        for (int y = 0; y < gh; ++y)
            for (int x = 0; x < gw; ++x) {
                const aboutcat::Pix& p = pix[(size_t)y * gw + x];
                // The silhouette and the eyes are drawn smooth below, not
                // in characters.
                if (p.ch == ' ' || (p.flags & aboutcat::PIX_SILHOUETTE) || p.tone == aboutcat::TONE_EYE)
                    continue;
                Rgba c = theme.c[kToneSlot[p.tone]];
                c.a = (uint8_t)(c.a * kToneAlpha[p.tone] / 255);
                const uint32_t cp = (uint32_t)(unsigned char)p.ch;
                stamp(x, y, cp, c, 0, weight[f]);
                // The lines in bold: thin strokes would come out of the
                // averaging faint.
                if (p.tone == aboutcat::TONE_LINE) stamp(x, y, cp, c, 1, weight[f]);
            }
    }

    // The distance to the silhouette at a pixel, in pixels: the cells'
    // distances sampled bilinearly — their zero runs smoothly between
    // cells — and, between two phases, blended like the rest, so the edge
    // moves rather than fades.
    const float cellPxW = (float)mw / SS, cellPxH = (float)mh / SS;
    const float pxPerUnit = cellPxH / aboutcat::UnitsPerRow(gw, gh, aspect);
    auto distAt = [&](const std::vector<aboutcat::Pix>& pix, float px, float py) {
        const float gx = std::clamp(px / cellPxW - 0.5f, 0.0f, (float)gw - 1.001f);
        const float gy = std::clamp(py / cellPxH - 0.5f, 0.0f, (float)gh - 1.001f);
        const int x0 = (int)gx, y0 = (int)gy;
        const int x1 = std::min(x0 + 1, gw - 1), y1 = std::min(y0 + 1, gh - 1);
        const float u = gx - x0, v = gy - y0;
        auto d = [&](int x, int y) { return (float)pix[(size_t)y * gw + x].dist; };
        return ((d(x0, y0) * (1 - u) + d(x1, y0) * u) * (1 - v) +
                (d(x0, y1) * (1 - u) + d(x1, y1) * u) * v) / aboutcat::DIST_SCALE * pxPerUnit;
    };

    // The shadow's ellipse, in pixels.
    float scx, scy, srx, sry;
    aboutcat::Ground(gw, gh, aspect, scx, scy, srx, sry);
    scx *= cellPxW; scy *= cellPxH; srx *= cellPxW; sry *= cellPxH;

    // The eyes, in pixels, at this frame's own turn — between two phases
    // they slide rather than fade. Blinking, the ring flattens and sinks a
    // little, as a lid comes down, until it is a line.
    struct Ring { bool on; float cx, cy, a, b, alpha, x0, y0, x1, y1; };
    Ring rings[2];
    {
        aboutcat::Eye eyes[2];
        aboutcat::EyeShapes(gw, gh, aspect, turn, blink, eyes);
        for (int k = 0; k < 2; ++k) {
            const aboutcat::Eye& e = eyes[k];
            Ring& g = rings[k];
            g.on = e.visible;
            if (!g.on) continue;
            g.a = e.a * cellPxW;
            g.b = std::max(e.b * cellPxH * e.open, 0.6f);
            g.cx = e.cx * cellPxW;
            g.cy = e.cy * cellPxH + e.b * cellPxH * 0.15f * (1.0f - e.open);
            // Fading in as the eye turns to face the viewer, rather than popping.
            g.alpha = std::clamp((e.facing - 0.2f) / 0.15f, 0.0f, 1.0f);
            g.x0 = g.cx - g.a - 2; g.x1 = g.cx + g.a + 2;
            g.y0 = g.cy - g.b - 2; g.y1 = g.cy + g.b + 2;
        }
    }
    // The distance from a pixel to a ring's ellipse, in pixels, negative
    // inside: the ellipse's implicit value over the length of its gradient.
    auto ringDist = [](const Ring& g, float px, float py) {
        const float qx = (px - g.cx) / g.a, qy = (py - g.cy) / g.b;
        const float len = std::sqrt(qx * qx + qy * qy);
        if (len < 1e-4f) return -std::min(g.a, g.b);
        const float gx = qx / g.a, gy = qy / g.b;
        return (len - 1.0f) * len / std::sqrt(gx * gx + gy * gy);
    };

    // Each pixel: the shadow, the characters over it, the outline over all.
    // Coverage goes through a square root on the way, as text renderers do
    // for thin strokes: a stroke a third of a pixel wide would otherwise all
    // but vanish, while solid ink stays solid.
    L.rgba.assign((size_t)W * H * 4, 0);
    const float full = 255.0f * 256.0f * SS * SS;
    const Rgba& ink = theme.c[TEXT];
    const Rgba& dark = theme.c[SHADOW];
    const float wmix = nFrames == 2 ? weight[1] / 256.0f : 0.0f;
    // Far from the edge the nearest cell's distance is close enough: the
    // full sample is taken only within a few pixels of it, and a pixel with
    // no ink, no line and no shadow is passed over at once.
    const float nearPx = 2.0f * std::max(cellPxW, cellPxH) + 1.5f;
    auto nearestDist = [&](const std::vector<aboutcat::Pix>& pix, float px, float py) {
        const int gx = std::clamp((int)(px / cellPxW), 0, gw - 1);
        const int gy = std::clamp((int)(py / cellPxH), 0, gh - 1);
        return pix[(size_t)gy * gw + gx].dist / aboutcat::DIST_SCALE * pxPerUnit;
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t)y * W + x;
            const float px = x + 0.5f, py = y + 0.5f;
            const float ex = (px - scx) / srx, ey = (py - scy) / sry, e = ex * ex + ey * ey;
            float dist = nearestDist(L.frame[0], px, py);
            const bool nearEdge = std::fabs(dist) < nearPx ||
                                  (nFrames == 2 && std::fabs(nearestDist(L.frame[1], px, py)) < nearPx);
            bool nearEye = false;
            for (const Ring& g : rings)
                nearEye = nearEye || (g.on && px >= g.x0 && px <= g.x1 && py >= g.y0 && py <= g.y1);
            if (!nearEdge && !nearEye && e >= 1.0f && L.acc[i * 4] == 0) continue;
            if (nearEdge) {
                dist = distAt(L.frame[0], px, py);
                if (nFrames == 2) dist += (distAt(L.frame[1], px, py) - dist) * wmix;
            }

            // Under everything: the shadow, soft all over, and stopping
            // softly at the cat's edge.
            float r = 0, g = 0, b = 0, a = 0;
            if (e < 1.0f && dist > 0.0f) {
                const float fall = (1.0f - e) * (1.0f - e);
                a = 0.65f * fall * std::min(1.0f, dist / 2.0f) * dark.a / 255.0f;
                r = dark.r; g = dark.g; b = dark.b;
            }
            // Over it, "over": the characters ...
            auto over = [&](float cr, float cg, float cb, float ca) {
                if (ca <= 0.0f) return;
                const float na = ca + a * (1.0f - ca);
                r = (cr * ca + r * a * (1.0f - ca)) / na;
                g = (cg * ca + g * a * (1.0f - ca)) / na;
                b = (cb * ca + b * a * (1.0f - ca)) / na;
                a = na;
            };
            // The rings' distances; inside one, the pupil, as black as the
            // cat — the coat is wiped there.
            float eyeD[2] = { 1e9f, 1e9f };
            float pupil = 0.0f;
            for (int k = 0; k < 2; ++k) {
                if (!nearEye || !rings[k].on) continue;
                eyeD[k] = ringDist(rings[k], px, py);
                pupil = std::max(pupil, std::clamp(-eyeD[k], 0.0f, 1.0f) * rings[k].alpha);
            }
            const uint32_t* s = &L.acc[i * 4];
            if (s[0]) {
                const uint32_t base = std::max<uint32_t>(1, s[0] / 255);
                over((float)std::min<uint32_t>(255, s[1] / base), (float)std::min<uint32_t>(255, s[2] / base),
                     (float)std::min<uint32_t>(255, s[3] / base),
                     std::sqrt(std::min(1.0f, (float)s[0] / full)) * (1.0f - pupil));
            }
            // ... the outline: full within a fifth of a pixel of the edge,
            // gone a pixel and a fifth out — about the weight of the bold
            // characters it stands in for ...
            over(ink.r, ink.g, ink.b, std::clamp(1.2f - std::fabs(dist), 0.0f, 1.0f) * ink.a / 255.0f);
            // ... and the eyes: rings a little heavier than the outline, as
            // on the sleeve, cut softly where the head's edge passes them.
            for (int k = 0; k < 2; ++k) {
                if (eyeD[k] > 2.0f) continue;
                const float cut = std::clamp(-dist - 0.5f, 0.0f, 1.0f);
                over(ink.r, ink.g, ink.b,
                     std::clamp(1.5f - std::fabs(eyeD[k]), 0.0f, 1.0f) * cut * rings[k].alpha * ink.a / 255.0f);
            }

            if (a <= 0.0f) continue;
            uint8_t* d = &L.rgba[i * 4];
            d[0] = (uint8_t)std::lround(r);
            d[1] = (uint8_t)std::lround(g);
            d[2] = (uint8_t)std::lround(b);
            d[3] = (uint8_t)std::lround(std::min(1.0f, a) * 255.0f);
        }

    if (L.store != &rasters || L.w != W || L.h != H || L.id < 0) {
        // The page only grows: a new size takes new room, so it happens once
        // per density, not per frame.
        L.id = rasters.addRGBA(L.rgba.data(), W, H);
        L.store = &rasters;
        L.w = W;
        L.h = H;
    } else {
        rasters.update(L.id, L.rgba.data());
    }
    if (L.id < 0) return false;

    // Centred in r to the pixel: the picture maps 1:1 onto the screen.
    const int ox = (r.w * cw - W) / 2, oy = (r.h * ch - H) / 2;
    ctx.cells().sprite(r.x + (float)ox / cw, r.y + (float)oy / ch, (float)W / cw, (float)H / ch,
                       (uint16_t)L.id);
    return true;
}

} // namespace

void DrawAboutCat(ui::Context& ctx, RasterStore* rasters, const Theme* theme, const ui::Rect& r,
                  float scale, double time, bool frozen) {
    if (r.empty()) return;
    // The swing's position in phases; frozen, a three-quarter view a little
    // off the front, eyes open.
    const double at = std::fmod(std::max(0.0, time) * kFps, (double)kPhases);
    const int phase = frozen ? -1 : std::min(kPhases - 1, (int)at);
    const float mix = frozen ? 0.0f : (float)(at - phase);
    const float turn = frozen ? 0.30f
                              : TurnAt(phase) + (TurnAt((phase + 1) % kPhases) - TurnAt(phase)) * mix;
    const float blink = frozen ? 0.0f : BlinkAt(std::max(0.0, time));
    const ui::Input& in = ctx.input();
    const bool layered = rasters && theme && scale > 0.0f && scale < 1.0f &&
                         DrawLayer(ctx, *rasters, *theme, r, scale, phase, mix, turn, blink);
    if (!layered) {
        const float aspect = in.cell_h > 0 ? (float)in.cell_w / (float)in.cell_h : 0.45f;
        DrawInCells(ctx, r, aspect, turn, blink);
    }
    // The blend moves between phases: twice the phase rate keeps it smooth.
    if (!frozen) ctx.animate(0.5 / kFps);
}
