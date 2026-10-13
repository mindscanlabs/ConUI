#include "Glyphs.hpp"

namespace Gly {

// Kept in step with Glyphs.hpp by hand; the coverage test walks this list, so
// a glyph that is used but not listed here simply is not checked. Add new
// constants in both places.
const uint32_t kAll[] = {
    H, V, TL, TR, BL, BR, TEE_R, TEE_L, TEE_D, TEE_U, CROSS, HALF_R, HALF_L,
    R_TL, R_TR, R_BR, R_BL,
    DASH_H, DASH_V,
    FULL, UPPER_HALF, LOWER_HALF, LEFT_HALF, RIGHT_HALF,
    // all eight left- and lower-eighths
    0x258F, 0x258E, 0x258D, 0x258C, 0x258B, 0x258A, 0x2589, 0x2588,
    0x2581, 0x2582, 0x2583, 0x2584, 0x2585, 0x2586, 0x2587,
    SHADE_25, SHADE_50, SHADE_75,
    SQ_FILLED, SQ_HOLLOW, DOT_FILL, DOT_EMPTY, DIAMOND,
    TRI_UP, TRI_DOWN, TRI_LEFT, TRI_RIGHT,
    ARROW_L, ARROW_U, ARROW_R, ARROW_D, ARROW_NE,
    BULLET, MIDDOT, CHECK, CROSS_X, TILDE, MISSING,
};
const int kAllCount = (int)(sizeof kAll / sizeof *kAll);

} // namespace Gly
