#include "Widgets.hpp"
#include "../../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// Scrollbars and scroll regions (§8.14).
//
// The thumb is the only widget that lives entirely in the shape layer: a
// pill half a cell wide, centred in its column, round-ended, positioned to
// the sub-pixel. No arrows, no track. When there is nothing to scroll there
// is nothing drawn at all.

namespace ui {

namespace {

const float kThumbW   = 0.55f;   // thickness, fraction of a cell column
const float kThumbPad = 0.30f;   // gap at each end of the run, cells
const float kThumbMin = 1.20f;   // shortest the thumb may get, cells
// The cell is 9 × 20, so "the same thickness in pixels" sideways is this
// fraction of a row (§8.14).
const float kCellAspect = 9.0f / 20.0f;

bool scrollbar(Context& ctx, const char* name, bool vertical,
               int x, int y, int len, int total, int visible, int& offset, bool live,
               bool centred = false) {
    const int maxOff = std::max(0, total - visible);
    offset = std::clamp(offset, 0, maxOff);
    if (maxOff == 0 || len < 2) return false;

    const Id id = ctx.makeId(name);
    const Rect r = vertical ? Rect{ x, y, 1, len } : Rect{ x, y, len, 1 };
    ctx.interact(id, r);                 // not focusable: the region is

    const Input& in = ctx.input();
    const float run = len - 2 * kThumbPad;
    const float tl = std::max(kThumbMin, run * (float)visible / (float)total);
    const float travel = std::max(0.0f, run - tl);
    const float origin = (vertical ? y : x) + kThumbPad;
    const float mouse = vertical ? in.mouse_y : in.mouse_x;

    // Where on the thumb the drag took hold, so it does not jump to centre
    // itself under the pointer. Negative = this press was a page click.
    float& grab = ctx.state(id, -1.0f);
    const int old = offset;

    auto thumbStart = [&]() {
        return origin + travel * ((float)offset / (float)maxOff);
    };
    if (ctx.justPressed(id)) {
        const float t0 = thumbStart();
        if (mouse >= t0 && mouse < t0 + tl) {
            grab = mouse - t0;
        } else {
            // A click beside the thumb pages toward the click — one page per
            // click, keeping a row of context (§8.14: arrows' job, done
            // without arrows).
            grab = -1.0f;
            offset += (mouse < t0 ? -1 : 1) * std::max(1, visible - 1);
        }
    } else if (ctx.isActive(id) && in.mouse_down && grab >= 0.0f && travel > 0.0f) {
        const float pos = (mouse - grab - origin) / travel;
        offset = (int)std::lround(std::clamp(pos, 0.0f, 1.0f) * maxOff);
    }
    offset = std::clamp(offset, 0, maxOff);

    const bool hot = ctx.isHot(id) || ctx.isActive(id);
    if (hot) ctx.setCursor(Cursor::Pointer);
    // The bar belongs to its region, not to the window: it shows while that
    // region is under the pointer or holds the focus, and is gone otherwise.
    // A drag that ran off the region keeps it — the thumb must not
    // vanish from under the hand that holds it.
    if (!live && !hot) return offset != old;
    // The same thickness under the pointer: only the colour says it is live:
    // a thumb that swelled on hover shifted against the frame.
    const float th = kThumbW;
    const uint8_t slot = hot ? (uint8_t)ACCENT : (uint8_t)ACCENT_DIM;
    const float t0 = thumbStart();
    CellBuffer& cb = ctx.cells();
    if (vertical) {
        cb.shape(x + (1.0f - th) * 0.5f, t0, th, tl, slot, 0.5f);
    } else {
        // Flush against the bottom of the row when a panel's border sits right
        // under it — centred there read as a gap below it; centred when there
        // is no border, so the one row holds it with equal air either side.
        const float hh = th * kCellAspect;
        cb.shape(t0, centred ? y + (1.0f - hh) * 0.5f : y + 1.0f - hh, tl, hh, slot, 0.5f);
    }
    return offset != old;
}

// Begin/End pairs nest, and EndScroll needs what BeginScroll computed.
// Windows are processed one after another, never interleaved, so a single
// stack serves every context and is empty between frames.
struct ScrollFrame {
    Rect r;
    int  pad_x, pad_y;
    int  origin_y;
    int  focus_start;
};
std::vector<ScrollFrame> g_scroll;

} // namespace

bool ScrollLive(Context& ctx, const Rect& r, bool focused) {
    if (focused) return true;
    const Input& in = ctx.input();
    return r.contains(in.mouse_x, in.mouse_y) && !ctx.blockedAt(in.mouse_x, in.mouse_y);
}

bool ScrollbarV(Context& ctx, const char* id, int x, int y, int h,
                int total, int visible, int& offset, bool live) {
    return scrollbar(ctx, id, true, x, y, h, total, visible, offset, live);
}

bool ScrollbarH(Context& ctx, const char* id, int x, int y, int w,
                int total, int visible, int& offset, bool live, bool centred) {
    return scrollbar(ctx, id, false, x, y, w, total, visible, offset, live, centred);
}

void BeginScroll(Context& ctx, const char* name, const Rect& r, int padX, int padY) {
    ctx.pushId(name);
    float& off = ctx.state(ctx.makeId("offset"));
    const float content = ctx.state(ctx.makeId("content"));

    // The bar is drawn HERE, against last frame's content height, so it
    // agrees with the content this frame draws. The wheel is handled in
    // EndScroll instead, after the content, so a slider or an inner list
    // under the pointer gets the wheel first.
    int o = (int)off;
    // Whether the focus is inside is only known once the content has been
    // laid out, so the bar goes by what EndScroll saw last frame; EndScroll
    // asks for another frame when that answer changes.
    const bool live = ScrollLive(ctx, r, ctx.state(ctx.makeId("focused")) > 0.5f);
    ScrollbarV(ctx, "bar", r.x + r.w - 1, r.y, r.h, (int)content, r.h, o, live);
    off = (float)o;

    ScrollFrame f;
    f.r = r;
    f.pad_x = padX;
    f.pad_y = padY;
    f.origin_y = r.y + padY - o;
    f.focus_start = ctx.focusableCount();
    g_scroll.push_back(f);

    // Clip to the region minus the bar's column; lay out from above the clip
    // by the offset. That is the entire mechanism.
    ctx.pushRegion(Rect{ r.x, r.y, r.w - 1, r.h }, r.x + padX, f.origin_y,
                   std::max(0, r.w - 1 - 2 * padX));
}

void EndScroll(Context& ctx) {
    if (g_scroll.empty()) return;
    const ScrollFrame f = g_scroll.back();
    g_scroll.pop_back();

    const int total = f.pad_y + (ctx.contentBottom() - f.origin_y) + f.pad_y;
    ctx.popRegion();

    float& off = ctx.state(ctx.makeId("offset"));
    ctx.state(ctx.makeId("content")) = (float)total;
    const int visible = f.r.h;
    const int maxOff = std::max(0, total - visible);
    int o = (int)off;

    const Input& in = ctx.input();
    const bool pointerIn = f.r.contains(in.mouse_x, in.mouse_y);
    const bool focusIn = ctx.focusIn(f.focus_start, ctx.focusableCount());

    // What BeginScroll will go by next frame. When it changes, the bar has to
    // appear or go, and with drawing on demand (§2) nothing else would ask
    // for that frame — Tab is done being an event by then.
    float& wasFocused = ctx.state(ctx.makeId("focused"));
    if ((wasFocused > 0.5f) != focusIn) ctx.animate();
    wasFocused = focusIn ? 1.0f : 0.0f;

    if (pointerIn) {
        const float w = ctx.takeWheel();
        if (w != 0.0f) o -= (int)std::lround(w * 3.0f);
    }
    if (pointerIn || focusIn) {
        if (in.key_pgup) o -= std::max(1, visible - 1);
        if (in.key_pgdn) o += std::max(1, visible - 1);
    }
    // Home/End belong to the focused widget if there is one inside (a slider
    // uses them); otherwise to the region under the pointer.
    if (pointerIn && !focusIn) {
        if (in.key_home) o = 0;
        if (in.key_end)  o = maxOff;
    }

    // Tab into a widget below the fold scrolls it into view — otherwise
    // keyboard focus walks off the bottom of the panel and disappears.
    Rect fr;
    if (focusIn && ctx.focusMovedByKey() && ctx.focusRect(fr)) {
        if (fr.y < f.r.y) o -= f.r.y - fr.y;
        else if (fr.y + fr.h > f.r.y + f.r.h) o += fr.y + fr.h - (f.r.y + f.r.h);
    }

    off = (float)std::clamp(o, 0, maxOff);
    ctx.popId();
}

int ScrollOffset(Context& ctx, const char* name) {
    ctx.pushId(name);
    const int o = (int)ctx.state(ctx.makeId("offset"));
    ctx.popId();
    return o;
}

} // namespace ui
