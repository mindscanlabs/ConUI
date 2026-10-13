#pragma once
#include "Context.hpp"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The split tree (docs/DESIGN_SYSTEM.md §8.17).
//
// A window's layout is a tree of splits, not a set of floating panels: drag
// the seam between two neighbours and they give and take from each other,
// the sum stays the same, and no gap can open up.
//
// In immediate mode the TREE is the call structure itself —
//
//     Rect left, right, top, bottom;
//     ui::Split(ctx, lay, "main", Axis::Cols, body, 0.4f, 20, 24, left, right);
//     ui::Split(ctx, lay, "right", Axis::Rows, right, 0.5f, 6, 6, top, bottom);
//
// — so nesting a call nests a node. What has to outlive the frame is only the
// fraction at each node, and that lives here, keyed by name, which is also
// what goes to data.bin.
// ---------------------------------------------------------------------------

namespace ui {

/** Cols: children side by side, a vertical seam. Rows: stacked, a horizontal one. */
enum class Axis : uint8_t { Cols, Rows };

class Layout {
public:
    /** The fraction at a node, created at `def` the first time it is asked for. */
    float& frac(const char* name, float def);
    /** The node's default, for the double-click reset. */
    float  defaultOf(const char* name) const;

    /** "name=0.4000;other=0.5000" — the data.bin form. Stable order. */
    std::string serialize() const;
    /**
     * Reads the form above. Unknown names are kept (the screen that owns them
     * may simply not have been drawn yet); malformed entries are skipped.
     */
    void deserialize(const std::string& s);

private:
    struct Node { std::string name; float value; float def; bool has_def; };
    std::vector<Node> nodes_;
    Node* find(const char* name);
    const Node* find(const char* name) const;
};

/**
 * Splits `r` into `a` and `b` with a one-cell seam between them, and makes
 * the seam draggable. The seam is empty window, with the splitter line in it.
 *
 *   * the step is pixel-precise, the result is whole cells: the stored
 *     fraction is continuous, the seam's POSITION is rounded to a cell, so
 *     content never sits between two columns;
 *   * each side has a minimum in cells, and the seam stops at it rather than
 *     collapsing a panel;
 *   * double-click the seam to return to the default fraction.
 *
 * Returns true while the seam is being dragged or was reset this frame.
 */
bool Split(Context& ctx, Layout& lay, const char* name, Axis axis, const Rect& r,
           float def, int minA, int minB, Rect& a, Rect& b);

} // namespace ui
