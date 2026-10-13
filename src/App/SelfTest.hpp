#pragma once
class FontAtlas;
class Renderer;
struct Theme;

/**
 * Renders one frame off-screen and checks the things that are easy to break
 * silently:
 *   1. every glyph in Gly::kAll is present in the shipped font's cmap;
 *   2. a solid field of █ has no seams at any cell size in 11..24 px;
 *   3. the built-in themes agree with the JSON files in themes/;
 *   4. the cell grid dumps back to the text it was built from.
 *
 * Returns 0 on success, non-zero on the first failure.
 */
int RunSelfTest(const char* outDir);
