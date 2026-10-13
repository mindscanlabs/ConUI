#include "Widgets.hpp"
#include "../../Render/CellBuffer.hpp"

#include <string>

namespace ui {

void Label(Context& ctx, const char* s, uint8_t slot, uint8_t attr) {
    if (!s) return;
    const Rect r = ctx.allocate(Utf8Width(s), 1);
    ctx.cells().text(r.x, r.y, s, ctx.disabled() ? (uint8_t)TEXT_MUTED : slot, BG_PANEL, attr);
}

void LabelFixed(Context& ctx, const char* s, int w, uint8_t slot) {
    const Rect r = ctx.allocate(w, 1);
    if (!s) return;
    // Cut to the column rather than spilling into the widget beside it. The
    // string table's width budget (§14) is what stops this from happening in
    // practice; this is the backstop.
    std::string cut;
    int n = 0;
    for (uint32_t cp : Utf8Decode(s)) {
        if (n++ >= w) break;
        Utf8Append(cut, cp);
    }
    ctx.cells().text(r.x, r.y, cut.c_str(), ctx.disabled() ? (uint8_t)TEXT_MUTED : slot, BG_PANEL);
}

void LabelRight(Context& ctx, const char* s, uint8_t slot, uint8_t attr) {
    if (!s) return;
    const int right = ctx.regionLeft() + ctx.regionWidth() - 1;
    ctx.cells().textRight(right, ctx.cursorY(), s,
                          ctx.disabled() ? (uint8_t)TEXT_MUTED : slot, BG_PANEL, attr);
}

} // namespace ui
