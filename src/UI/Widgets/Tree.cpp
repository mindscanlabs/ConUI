#include "Widgets.hpp"
#include "../Glyphs.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

// The directory tree (§8.7):
//
//   ▼ conui
//     ├─▼ docs
//     │   ├── REFERENCES.md           4.1K
//     │   └── DESIGN_SYSTEM.md        9.7K
//     ├─▶ refs                        (13)
//     └─▐ UI                            ▌   ← selection, full width
//
// Connectors ├── └── │. The tree is flattened every frame into the rows
// that are actually visible; nothing about the layout is cached.

namespace ui {

namespace {

struct Row {
    TreeNode* node;
    TreeNode* parent;
    int       depth;
    bool      last;          // last among its siblings: └ instead of ├
    uint32_t  guides;        // bit d: the ancestor at depth d has siblings below → │
};

void flatten(std::vector<TreeNode>& v, int depth, TreeNode* parent, uint32_t guides,
             std::vector<Row>& out) {
    for (size_t i = 0; i < v.size(); ++i) {
        TreeNode* n = &v[i];
        const bool last = i + 1 == v.size();
        out.push_back(Row{ n, parent, depth, last, guides });
        if (n->isDir() && n->open) {
            uint32_t g = guides;
            if (depth >= 1 && !last && depth < 32) g |= 1u << depth;
            flatten(n->children, depth + 1, n, g, out);
        }
    }
}

bool contains(const TreeNode& root, const TreeNode* n) {
    for (const TreeNode& c : root.children)
        if (&c == n || contains(c, n)) return true;
    return false;
}

int indexOf(const std::vector<Row>& rows, const TreeNode* n) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].node == n) return (int)i;
    return -1;
}

/** Column of the ▼/▶ for a row, relative to the tree's left margin. */
int expanderCol(int depth) { return depth == 0 ? 0 : 2 + (depth - 1) * 4 + 2; }
/** Column the label starts at. */
int labelCol(int depth) { return depth == 0 ? 2 : 2 + (depth - 1) * 4 + 4; }

struct TreeState { int scroll_y = 0; };

} // namespace

int Tree(Context& ctx, const char* name, const Rect& r,
         std::vector<TreeNode>& roots, const TreeNode*& selected) {
    if (r.w < 8 || r.h < 1) return 0;
    CellBuffer& cb = ctx.cells();
    const Input& in = ctx.input();
    ctx.pushId(name);
    const Id id = ctx.makeId("rows");
    TreeState& st = ctx.blob<TreeState>(id);
    int events = 0;

    const Rect hit{ r.x, r.y, r.w - 1, r.h };           // the last column is the bar's
    const int x0 = r.x + 1;                              // one cell of margin
    ctx.focusable(id);
    ctx.interact(id, hit);
    const bool focused = ctx.isFocused(id) && !ctx.disabled();

    std::vector<Row> rows;
    flatten(roots, 0, nullptr, 0, rows);
    int sel = indexOf(rows, selected);
    bool follow = false;

    auto toggle = [&](TreeNode* n) {
        n->open = !n->open;
        events |= TREE_TOGGLE;
        // Folding away the selection moves it to the folder that hid it,
        // rather than leaving the keyboard pointing at nothing visible.
        if (!n->open && selected && contains(*n, selected)) {
            selected = n;
            events |= TREE_SELECT;
        }
    };
    auto choose = [&](int i) {
        if (rows.empty()) return;
        i = std::clamp(i, 0, (int)rows.size() - 1);
        if (rows[(size_t)i].node != selected) { selected = rows[(size_t)i].node; events |= TREE_SELECT; }
        sel = i;
        follow = true;
    };

    // ---- the pointer ---------------------------------------------------------------
    if (ctx.justPressed(id)) {
        const int i = st.scroll_y + (int)std::floor(in.mouse_y - (float)r.y);
        if (i >= 0 && i < (int)rows.size()) {
            Row& row = rows[(size_t)i];
            const int ex = x0 + expanderCol(row.depth);
            const bool onExpander = row.node->isDir() &&
                                    in.mouse_x >= (float)ex && in.mouse_x < (float)(ex + 1);
            if (onExpander) {
                toggle(row.node);
            } else {
                choose(i);
                if (in.mouse_double) {
                    if (row.node->isDir()) toggle(row.node);
                    else events |= TREE_ACTIVATE;
                }
            }
        }
    }

    // ---- the keyboard ----------------------------------------------------------------
    if (focused && !rows.empty()) {
        const int page = std::max(1, r.h - 1);
        if (in.key_up)   choose(sel < 0 ? 0 : sel - 1);
        if (in.key_down) choose(sel < 0 ? 0 : sel + 1);
        if (in.key_pgup) choose(sel - page);
        if (in.key_pgdn) choose(sel + page);
        if (in.key_home) choose(0);
        if (in.key_end)  choose((int)rows.size() - 1);
        if (sel >= 0) {
            Row& row = rows[(size_t)sel];
            if (in.key_right && row.node->isDir()) {
                if (!row.node->open) toggle(row.node);
                else if (!row.node->children.empty()) {
                    selected = &row.node->children.front();
                    events |= TREE_SELECT;
                    follow = true;
                }
            }
            if (in.key_left) {
                if (row.node->isDir() && row.node->open) toggle(row.node);
                else if (row.parent) { selected = row.parent; events |= TREE_SELECT; follow = true; }
            }
            if (in.key_enter || in.key_space) {
                if (row.node->isDir()) toggle(row.node);
                else events |= TREE_ACTIVATE;
            }
        }
    }

    // Toggles changed the shape; flatten again for drawing.
    if (events & (TREE_TOGGLE | TREE_SELECT)) {
        rows.clear();
        flatten(roots, 0, nullptr, 0, rows);
        sel = indexOf(rows, selected);
    }
    if (hit.contains(in.mouse_x, in.mouse_y)) {
        const float w = ctx.takeWheel();
        if (w != 0.0f) st.scroll_y -= (int)std::lround(w * 3.0f);
    }
    const int count = (int)rows.size();
    if (follow && sel >= 0) {
        if (sel < st.scroll_y) st.scroll_y = sel;
        if (sel >= st.scroll_y + r.h) st.scroll_y = sel - r.h + 1;
    }
    st.scroll_y = std::clamp(st.scroll_y, 0, std::max(0, count - r.h));

    // ---- draw -----------------------------------------------------------------------
    ctx.pushRegion(hit);
    const bool hotRows = ctx.isHot(id) && hit.contains(in.mouse_x, in.mouse_y);
    if (hotRows) ctx.setCursor(Cursor::Pointer);
    const int hotRow = hotRows ? st.scroll_y + (int)std::floor(in.mouse_y - (float)r.y) : -1;
    const int right = r.x + r.w - 2;                     // last column before the bar
    for (int k = 0; k < r.h; ++k) {
        const int i = st.scroll_y + k;
        if (i >= count) break;
        const Row& row = rows[(size_t)i];
        const TreeNode& n = *row.node;
        const int y = r.y + k;
        const bool isSel = i == sel;
        const bool isHot = i == hotRow && !isSel;

        // guides and connector
        for (int d = 1; d < row.depth; ++d)
            if (row.guides & (1u << d))
                cb.put(x0 + 2 + (d - 1) * 4, y, Gly::V, BORDER_DIM, BG_PANEL);
        const int ex = x0 + expanderCol(row.depth);
        if (row.depth >= 1) {
            const int cx = ex - 2;
            cb.put(cx, y, row.last ? Gly::BL : Gly::TEE_R, BORDER_DIM, BG_PANEL);
            cb.put(cx + 1, y, Gly::H, BORDER_DIM, BG_PANEL);
        }
        if (n.isDir()) cb.put(ex, y, n.open ? Gly::TRI_DOWN : Gly::TRI_RIGHT, ACCENT, BG_PANEL);
        else if (row.depth >= 1) cb.put(ex, y, Gly::H, BORDER_DIM, BG_PANEL);

        const int lx = x0 + labelCol(row.depth);
        const uint8_t bg = isSel ? (uint8_t)BG_SEL : isHot ? (uint8_t)BG_RAISED : (uint8_t)BG_PANEL;
        const uint8_t fg = isSel ? (uint8_t)TEXT_INV : n.isDir() ? (uint8_t)TEXT : (uint8_t)TEXT2;
        if (isSel || isHot) {
            cb.put(lx - 1, y, Gly::RIGHT_HALF, bg, BG_PANEL);
            cb.bgRect(lx, y, right - lx, 1, bg);
            cb.put(right, y, Gly::LEFT_HALF, bg, BG_PANEL);
        }

        // meta: a size, or the child count of a folded folder
        char count_[16];
        const char* meta = n.meta.empty() ? nullptr : n.meta.c_str();
        if (!meta && n.isDir() && !n.open && !n.children.empty()) {
            std::snprintf(count_, sizeof count_, "(%d)", (int)n.children.size());
            meta = count_;
        }
        // The label has priority over the meta: when both do not fit, the
        // size goes, and a label that still does not fit is cut with "…" —
        // visibly, never silently.
        std::vector<uint32_t> label = Utf8Decode(n.label.c_str());
        const int metaW = meta ? Utf8Width(meta) : 0;
        const int room = std::max(0, right - lx);
        const bool showMeta = meta && (int)label.size() + 1 + metaW <= room - 1;
        if ((int)label.size() > room) {
            label.resize((size_t)room);
            if (room > 0) label.back() = 0x2026;
        }
        int c = 0;
        for (uint32_t cp : label)
            cb.put(lx + c++, y, cp, fg, bg, n.isDir() && !isSel ? ATTR_BOLD : ATTR_NONE);
        if (showMeta)
            cb.textRight(right - 1, y, meta, isSel ? (uint8_t)TEXT_INV : (uint8_t)TEXT_MUTED, bg);
    }
    ctx.popRegion();

    ScrollbarV(ctx, "bar", r.x + r.w - 1, r.y, r.h, count, r.h, st.scroll_y,
               ScrollLive(ctx, r, focused));
    ctx.popId();
    return events;
}

} // namespace ui
