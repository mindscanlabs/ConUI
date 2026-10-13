#pragma once
#include "Widgets.hpp"

#include <cstdint>
#include <vector>

// What Table and EditTable share (§8.8, §8.19): the column layout, the frame
// with its title and row count, and the sortable header. Internal to Widgets/.

namespace ui::table {

/** Cuts a line to `w` cells; the cut is visible as …, not silent. */
std::vector<uint32_t> Fit(std::vector<uint32_t> t, int w);

/**
 * Column widths with their one-cell padding on each side; the last column
 * stretches to fill `innerW`. Returns the total width, separators included —
 * more than `innerW` when the columns overflow.
 */
int LayoutColumns(const TableColumn* cols, int n, int innerW, std::vector<int>& cw);

/**
 * The frame, the title and " N rows " in the top border, and the raised
 * ground of the header row at `headerY`. No rule under it.
 */
void DrawFrame(CellBuffer& cb, const Rect& r, uint8_t edge, const char* title, int rows,
               int headerY);

/**
 * The header row: titles left-aligned, the sort arrow at the column's right
 * edge, │ between columns, carrying on into the rows right below it. A click
 * sorts by that column, a second click flips it; true when that happened.
 */
bool Header(Context& ctx, const TableColumn* cols, int n, const std::vector<int>& xs,
            const std::vector<int>& cw, int headerY, int& sortCol, bool& sortDesc);

} // namespace ui::table
