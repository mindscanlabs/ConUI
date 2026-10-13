#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// The approved glyph vocabulary (docs/DESIGN_SYSTEM.md §4). Widgets take
// codepoints from here and nowhere else — the build checks that every name in
// this file is present in the shipped font's cmap, so a missing
// glyph is a compile-time failure rather than a □ at runtime.
//
// Note what is absent: there are NO double lines (═ ║ ╔ …) and no mixed
// junctions. Frame weight is uniform across the whole system — window, panel
// and modal alike. Hierarchy is carried by colour and inversion, never by line
// weight (§8.9).
// ---------------------------------------------------------------------------

namespace Gly {

// Frames — one weight, used by every container there is.
constexpr uint32_t H       = 0x2500; // ─
constexpr uint32_t V       = 0x2502; // │
constexpr uint32_t TL      = 0x250C; // ┌
constexpr uint32_t TR      = 0x2510; // ┐
constexpr uint32_t BL      = 0x2514; // └
constexpr uint32_t BR      = 0x2518; // ┘
constexpr uint32_t TEE_R   = 0x251C; // ├
constexpr uint32_t TEE_L   = 0x2524; // ┤
constexpr uint32_t TEE_D   = 0x252C; // ┬
constexpr uint32_t TEE_U   = 0x2534; // ┴
constexpr uint32_t CROSS   = 0x253C; // ┼
// Half lines: a line that starts at the cell's centre — a slider's ends,
// under the short end tick the slider draws itself (§8.4).
constexpr uint32_t HALF_R  = 0x2576; // ╶
constexpr uint32_t HALF_L  = 0x2574; // ╴

// Rounded corners, for the rare softer container.
constexpr uint32_t R_TL    = 0x256D; // ╭
constexpr uint32_t R_TR    = 0x256E; // ╮
constexpr uint32_t R_BR    = 0x256F; // ╯
constexpr uint32_t R_BL    = 0x2570; // ╰

// Dashed.
constexpr uint32_t DASH_H  = 0x2504; // ┄
constexpr uint32_t DASH_V  = 0x2506; // ┆

// Blocks. The eighths give sub-cell precision to progress bars and sliders
// without leaving the grid (§8.3, §8.4).
constexpr uint32_t FULL       = 0x2588; // █
constexpr uint32_t UPPER_HALF = 0x2580; // ▀
constexpr uint32_t LOWER_HALF = 0x2584; // ▄
constexpr uint32_t LEFT_HALF  = 0x258C; // ▌
constexpr uint32_t RIGHT_HALF = 0x2590; // ▐

/** Left n/8 of the cell, n = 1..8. */
constexpr uint32_t LeftEighths(int n)  { return n >= 8 ? FULL : (uint32_t)(0x2590 - n); }
/** Lower n/8 of the cell, n = 1..8. */
constexpr uint32_t LowerEighths(int n) { return (uint32_t)(0x2580 + n); }

// Shades — the stand-in for gradients: dithering (§4).
constexpr uint32_t SHADE_25 = 0x2591; // ░
constexpr uint32_t SHADE_50 = 0x2592; // ▒
constexpr uint32_t SHADE_75 = 0x2593; // ▓

// Geometry.
constexpr uint32_t SQ_FILLED = 0x25A0; // ■  slider knob
constexpr uint32_t SQ_HOLLOW = 0x25A1; // □  maximise button
constexpr uint32_t DOT_FILL  = 0x25CF; // ●  radio on, status marker
constexpr uint32_t DOT_EMPTY = 0x25CB; // ○  radio off
constexpr uint32_t DIAMOND   = 0x25C6; // ◆
constexpr uint32_t TRI_UP    = 0x25B2; // ▲  scrollbar, sort
constexpr uint32_t TRI_DOWN  = 0x25BC; // ▼
constexpr uint32_t TRI_LEFT  = 0x25C0; // ◀
constexpr uint32_t TRI_RIGHT = 0x25B6; // ▶  tree expander, submenu

// Arrows. NE is the fullscreen caption button (§8.1).
constexpr uint32_t ARROW_L  = 0x2190; // ←
constexpr uint32_t ARROW_U  = 0x2191; // ↑
constexpr uint32_t ARROW_R  = 0x2192; // →
constexpr uint32_t ARROW_D  = 0x2193; // ↓
constexpr uint32_t ARROW_NE = 0x2197; // ↗

// Markers.
constexpr uint32_t BULLET  = 0x2022; // •
constexpr uint32_t MIDDOT  = 0x00B7; // ·
constexpr uint32_t CHECK   = 0x2713; // ✓
constexpr uint32_t CROSS_X = 0x2717; // ✗
constexpr uint32_t TILDE   = 0x007E; // ~  mixed checkbox, past-EOF line
constexpr uint32_t MISSING = 0x25A1; // □  drawn when the font lacks a glyph

/** Every codepoint above, for the build-time font coverage check. */
extern const uint32_t kAll[];
extern const int kAllCount;

} // namespace Gly
