#include "Context.hpp"
#include "../Render/CellBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui {

namespace {

/** FNV-1a. Cheap, stable, and good enough to keep sibling widgets apart. */
Id hashStr(Id seed, const char* s) {
    Id h = seed ? seed : 1469598103934665603ull;
    for (const char* p = s; p && *p; ++p) {
        h ^= (Id)(unsigned char)*p;
        h *= 1099511628211ull;
    }
    return h ? h : 1;      // never collide with kNoId
}

/** FNV-1a over '#' and the int's four bytes: no formatting on every call. */
Id hashInt(Id seed, int i) {
    Id h = hashStr(seed, "#");
    for (int k = 0; k < 4; ++k) {
        h ^= (Id)(((uint32_t)i >> (8 * k)) & 0xFFu);
        h *= 1099511628211ull;
    }
    return h ? h : 1;
}

} // namespace

void Context::begin(CellBuffer& cells, const Input& in) {
    cells_ = &cells;
    in_ = in;

    id_stack_.clear();
    regions_.clear();
    Region root;
    root.clip = Rect{ 0, 0, cells.width(), cells.height() };
    root.layout_w = cells.width();
    regions_.push_back(root);
    cur_x_ = 0;
    cur_y_ = 0;
    line_h_ = 1;

    // Hover is resolved one frame behind on purpose. A widget has to know
    // whether it is hovered at the moment it draws, but the topmost widget
    // under the pointer is only known once everything has been through — so
    // the frame collects, and the next frame reports. At 60 fps nobody can
    // tell, and it costs no second pass.
    hovered_prev_ = hovered_;
    hovered_ = kNoId;
    next_frame_ = -1.0;

    focusables_.clear();
    focus_rect_valid_ = false;
    want_focus_next_ = in_.key_tab && !in_.key_shift;
    want_focus_prev_ = in_.key_tab && in_.key_shift;

    disabled_depth_ = 0;
    forced_ = forced_depth_ = 0;
    disabled_stack_.clear();
    wheel_taken_ = false;
    cursor_ = Cursor::Arrow;
    text_captured_ = false;
    escape_claimed_ = false;
    keys_claimed_prev_ = keys_claimed_now_;
    keys_claimed_now_ = false;

    // Overlays claim input one frame behind, exactly like hover: what was on
    // top LAST frame decides who sees the pointer now.
    overlay_prev_.swap(overlay_now_);
    overlay_now_.clear();
    overlay_depth_ = 0;
    overlay_stack_.clear();
    modal_prev_ = modal_now_;
    modal_now_ = false;
    modal_focus_from_ = modal_focus_to_ = -1;
    modal_preferred_ = kNoId;
    last_id_ = kNoId;
    clipboard_set_ = false;
    clipboard_out_.clear();

    // The mouse is released everywhere, not just over the active widget:
    // otherwise letting go outside a button leaves it stuck down forever.
    if (!in_.mouse_down && active_ != kNoId && !in_.mouse_released)
        active_ = kNoId;
}

namespace {
constexpr double kScreenGlitchDur   = 0.21;   // three steps of 70 ms, like the element glitch
} // namespace

bool Context::glitchScreen(ScreenGlitch kind) {
    if (!glitches_ || forced_depth_) return false;
    // One at a time: a request while one plays is dropped; the next one is
    // free the moment it ends.
    if (sg_start_ >= 0.0 && in_.time - sg_start_ < kScreenGlitchDur) return false;
    sg_start_ = in_.time;
    sg_kind_ = kind;
    const uint64_t bits = (uint64_t)(in_.time * 1000.0);
    sg_seed_ = Mix32((uint32_t)bits ^ (uint32_t)(bits >> 32) ^ 0xA511E9B3u);
    return true;
}

void Context::applyScreenGlitch() {
    if (sg_start_ < 0.0 || !cells_) return;
    animate();                       // a glitch runs on frames, not on events
    const double t = in_.time - sg_start_;
    if (!glitches_ || t < 0.0 || t >= kScreenGlitchDur) { sg_start_ = -1.0; return; }
    const int H = cells_->height();
    if (H <= 0) return;
    const int step = (int)(t / (kScreenGlitchDur / 3.0));
    const uint32_t s = Mix32(sg_seed_ ^ (uint32_t)(step + 1) * 0x9E3779B9u);
    const int bands = 2 + (int)(s % 3u);
    for (int b = 0; b < bands; ++b) {
        const uint32_t h = Mix32(s ^ (uint32_t)(b + 1) * 0x85EBCA6Bu);
        const int y0 = (int)(h % (uint32_t)H);
        const int bh = 1 + (int)((h >> 8) % (uint32_t)std::max(1, H / 6));
        const int dx = ((h >> 20) & 1u ? 1 : -1) * (1 + (int)((h >> 21) % 4u));
        cells_->glitchRows(y0, y0 + bh, dx, sg_kind_ == ScreenGlitch::Tear);
    }
}

void Context::end() {
    // Last, over everything the frame drew: the whole-window glitch (§9).
    applyScreenGlitch();

    // Hover timing for tooltips: the clock restarts whenever the item under
    // the pointer changes.
    if (hovered_ != hover_tracked_) { hover_tracked_ = hovered_; hover_since_ = in_.time; }
    if (focus_moved_by_key_ > 0) --focus_moved_by_key_;
    resolveTab();
    if (in_.mouse_released) active_ = kNoId;
    // An active widget that was not drawn this frame (scrolled away, hidden,
    // its panel collapsed) must not keep the mouse captured. interact() gives
    // the active widget the hover unconditionally, so "not hovered" here
    // means "not drawn".
    if (active_ != kNoId && hovered_ != active_) active_ = kNoId;
}

void Context::resolveTab() {
    // The frame a modal opens, the content's widgets were still registered;
    // from the next frame on they are not (focusable()). Either way the Tab
    // order is only the modal's own, and focus starts inside it.
    if (modal_now_ && modal_focus_from_ >= 0 && modal_focus_to_ > modal_focus_from_) {
        std::vector<Id> inside(focusables_.begin() + modal_focus_from_,
                               focusables_.begin() + modal_focus_to_);
        focusables_.swap(inside);
        if (std::find(focusables_.begin(), focusables_.end(), focus_) == focusables_.end())
            focus_ = std::find(focusables_.begin(), focusables_.end(), modal_preferred_) !=
                     focusables_.end() ? modal_preferred_ : focusables_.front();
    }
    if (focusables_.empty()) { focus_ = kNoId; return; }

    // Focus must survive widgets appearing and disappearing: if the focused id
    // is gone this frame, fall back to the first one rather than to nothing.
    auto it = std::find(focusables_.begin(), focusables_.end(), focus_);
    int idx = (it == focusables_.end()) ? -1 : (int)(it - focusables_.begin());

    if (want_focus_next_) idx = (idx + 1) % (int)focusables_.size();
    else if (want_focus_prev_) idx = (idx <= 0 ? (int)focusables_.size() : idx) - 1;
    else if (idx < 0) idx = -1;          // nothing focused, and no Tab pressed

    if (want_focus_next_ || want_focus_prev_) focus_moved_by_key_ = 1;
    focus_ = (idx >= 0) ? focusables_[(size_t)idx] : kNoId;
}

// ---- identity -------------------------------------------------------------

void Context::pushId(const char* s) {
    id_stack_.push_back(hashStr(id_stack_.empty() ? 0 : id_stack_.back(), s));
}
void Context::pushId(int i) {
    id_stack_.push_back(hashInt(id_stack_.empty() ? 0 : id_stack_.back(), i));
}
void Context::popId() {
    if (!id_stack_.empty()) id_stack_.pop_back();
}
Id Context::makeId(const char* s) const {
    return hashStr(id_stack_.empty() ? 0 : id_stack_.back(), s);
}
Id Context::makeId(int i) const {
    return hashInt(id_stack_.empty() ? 0 : id_stack_.back(), i);
}

// ---- layout ---------------------------------------------------------------

void Context::pushRegion(const Rect& r) { pushRegion(r, r.x, r.y); }

void Context::pushRegion(const Rect& r, int originX, int originY, int layoutW) {
    pushRegionRaw(r, originX, originY, layoutW, false);
}

void Context::pushRegionRaw(const Rect& r, int originX, int originY, int layoutW,
                            bool absolute) {
    // Regions clip against their parent, so a panel that does not fit cannot
    // draw outside the window by accident — and the clip is handed to the
    // cell buffer, so that holds for drawing as well as for input. An
    // ABSOLUTE region (an overlay) clips against the screen instead: a menu
    // opened from inside a scrolled panel must be able to leave the panel.
    const Rect& p = absolute ? regions_.front().clip : regions_.back().clip;
    Region g;
    g.clip.x = std::max(r.x, p.x);
    g.clip.y = std::max(r.y, p.y);
    g.clip.w = std::max(0, std::min(r.x + r.w, p.x + p.w) - g.clip.x);
    g.clip.h = std::max(0, std::min(r.y + r.h, p.y + p.h) - g.clip.y);
    g.ox = originX;
    g.oy = originY;
    g.layout_w = layoutW >= 0 ? layoutW : r.w;
    g.max_y = originY;
    g.saved_x = cur_x_;
    g.saved_y = cur_y_;
    g.saved_line_h = line_h_;
    regions_.push_back(g);
    if (absolute) cells_->pushClipReset(g.clip.x, g.clip.y, g.clip.w, g.clip.h);
    else          cells_->pushClip(g.clip.x, g.clip.y, g.clip.w, g.clip.h);
    cur_x_ = g.ox;
    cur_y_ = g.oy;
    line_h_ = 1;
}

void Context::popRegion() {
    if (regions_.size() <= 1) return;
    const Region g = regions_.back();
    regions_.pop_back();
    cells_->popClip();
    // A region is a box of its own: the parent's cursor is where it was
    // before the push. What the parent does learn is how far down the box
    // reached — but only as far as it is VISIBLE, or a scroll region with a
    // hundred rows of content would make its parent a hundred rows tall.
    regions_.back().max_y = std::max(regions_.back().max_y,
                                     std::min(g.max_y, g.clip.y + g.clip.h));
    cur_x_ = g.saved_x;
    cur_y_ = g.saved_y;
    line_h_ = g.saved_line_h;
}

Rect Context::allocate(int w, int h) {
    Region& r = regions_.back();
    if (cur_x_ + w > r.ox + r.layout_w && cur_x_ > r.ox) newline();   // wrap
    Rect out{ cur_x_, cur_y_, w, h };
    cur_x_ += w;
    line_h_ = std::max(line_h_, h);
    r.max_y = std::max(r.max_y, out.y + h);
    return out;
}

Rect Context::allocateRow(int h) {
    const Region& r = regions_.back();
    const int w = std::max(0, r.ox + r.layout_w - cur_x_);
    return allocate(w, h);
}

void Context::newline(int rows) {
    const Region& r = regions_.back();
    cur_x_ = r.ox;
    cur_y_ += (rows > 0 ? rows : 1) * line_h_;
    line_h_ = 1;
    regions_.back().max_y = std::max(regions_.back().max_y, cur_y_);
}

void Context::spacer(int cells) { cur_x_ += cells; }

void Context::column(int x) { cur_x_ = regions_.back().ox + x; }

// ---- interaction ----------------------------------------------------------

void Context::focusable(Id id) {
    if (id == kNoId || disabled() || forced_depth_) return;
    // Under an open modal the content drops out of the Tab order: focus is
    // trapped inside the modal (§8.9).
    if (modal_prev_ && overlay_depth_ == 0) return;
    focusables_.push_back(id);
}

bool Context::focusActivated(Id id) const {
    return isFocused(id) && !disabled() && (in_.key_enter || in_.key_space);
}

bool Context::focusIn(int from, int to) const {
    if (focus_ == kNoId) return false;
    to = std::min(to, (int)focusables_.size());
    for (int i = std::max(0, from); i < to; ++i)
        if (focusables_[(size_t)i] == focus_) return true;
    return false;
}

bool Context::focusRect(Rect& out) const {
    if (!focus_rect_valid_) return false;
    out = focus_rect_;
    return true;
}

bool Context::interact(Id id, const Rect& r) {
    return interact(id, r, (float)r.x, (float)r.y, (float)r.w, (float)r.h);
}

bool Context::interact(Id id, const Rect& r, float hx, float hy, float hw, float hh) {
    if (id == kNoId) return false;
    if (id == focus_) { focus_rect_ = r; focus_rect_valid_ = true; }
    last_id_ = id;
    if (forced_depth_) return false;          // a picture of a state takes no input
    if (disabled()) return false;

    const Rect& clip = regions_.back().clip;
    const bool visible = r.x < clip.x + clip.w && r.y < clip.y + clip.h &&
                         r.x + r.w > clip.x && r.y + r.h > clip.y;
    // The pointer has to be inside the clip too, not just inside the widget:
    // a row scrolled half out of a list must not be clickable through the
    // panel's frame.
    const bool inHit = in_.mouse_x >= hx && in_.mouse_x < hx + hw &&
                       in_.mouse_y >= hy && in_.mouse_y < hy + hh;
    const bool over = visible && inHit &&
                      clip.contains(in_.mouse_x, in_.mouse_y) &&
                      !blockedAt(in_.mouse_x, in_.mouse_y);   // under an overlay: not this widget's

    // Later widgets win the hover, which matches draw order: whatever is drawn
    // last is on top, so it is what the pointer is actually touching. A held
    // widget keeps the hover, so a drag does not light up whatever it crosses.
    if (over && (active_ == kNoId || active_ == id)) hovered_ = id;
    if (active_ == id) hovered_ = id;

    bool clicked = false;
    if (over && in_.mouse_pressed && active_ == kNoId) {
        active_ = id;
        // Clicking moves keyboard focus too — but only to something that is
        // in the Tab order. A scrollbar, a seam or a menu row is not, and a
        // click on one must leave the focus where it was.
        if (!focusables_.empty() && focusables_.back() == id) focus_ = id;
    }
    if (active_ == id) active_over_ = over;
    if (in_.mouse_released && active_ == id && over) clicked = true;

    // Keyboard activation is not a second code path: Enter or Space on the
    // focused item produces exactly the same result as a click (§9).
    if (focusActivated(id)) clicked = true;

    return clicked;
}

void Context::beginDisabled(bool d) {
    disabled_stack_.push_back(d ? 1 : 0);
    if (d) ++disabled_depth_;
}

void Context::endDisabled() {
    if (disabled_stack_.empty()) return;
    if (disabled_stack_.back()) --disabled_depth_;
    disabled_stack_.pop_back();
}

float& Context::state(Id id, float init) {
    auto it = store_.find(id);
    if (it == store_.end()) it = store_.emplace(id, init).first;
    return it->second;
}

float Context::takeWheel() {
    if (wheel_taken_ || disabled()) return 0.0f;
    if (blockedAt(in_.mouse_x, in_.mouse_y)) return 0.0f;   // the overlay on top scrolls, not this region
    if (in_.wheel == 0.0f) return 0.0f;
    wheel_taken_ = true;
    return in_.wheel;
}

bool Context::blinkOn() const {
    // Asking for the frame the blink next changes on is what keeps every
    // cursor and focus diamond alive without redrawing at 60 Hz (§9).
    const double phase = std::fmod(in_.time, 1.06);
    animate(phase < 0.53 ? 0.53 - phase : 1.06 - phase);
    return phase < 0.53;
}

// ---- overlays --------------------------------------------------------------

bool Context::blockedAt(float x, float y) const {
    // Everything below a modal is inert, wherever the pointer is.
    if (modal_prev_ && overlay_depth_ == 0) return true;
    for (const OverlayRect& o : overlay_prev_)
        if (o.level > overlay_depth_ && o.r.contains(x, y)) return true;
    return false;
}

void Context::beginOverlay(const Rect& r, bool blocking) {
    ++overlay_depth_;
    if (blocking) overlay_now_.push_back(OverlayRect{ r, overlay_depth_ });
    overlay_stack_.push_back(OverlaySave{ (int)cells_->band() });
    cells_->setBand(Z_OVERLAY);
    pushRegionRaw(r, r.x, r.y, r.w, /*absolute=*/true);
}

void Context::endOverlay() {
    if (overlay_stack_.empty()) return;
    popRegion();
    cells_->setBand((ZBand)overlay_stack_.back().saved_band);
    overlay_stack_.pop_back();
    --overlay_depth_;
}

void Context::beginModal(const Rect& r) {
    modal_now_ = true;
    cells_->setDimBase(true);
    beginOverlay(r, true);
    modal_focus_from_ = (int)focusables_.size();
}

void Context::endModal() {
    modal_focus_to_ = (int)focusables_.size();
    endOverlay();
}

double Context::hoverSeconds(Id id) const {
    if (id == kNoId || id != hover_tracked_ || hovered_prev_ != id) return 0.0;
    return in_.time - hover_since_;
}

// ---- forced states (the STATES screen) ---------------------------------------

void Context::beginForced(int flags) {
    if (forced_depth_) return;
    // The real input is parked and an empty one stands in: a widget drawn as
    // "focused" must not also start reacting to the keyboard. Only the clock
    // survives, so the focus marker and the cursor still blink — and the cell
    // size, which is geometry, not input (whole-pixel shapes need it).
    forced_saved_in_ = in_;
    Input quiet;
    quiet.time = in_.time;
    quiet.cell_w = in_.cell_w;
    quiet.cell_h = in_.cell_h;
    in_ = quiet;
    forced_ = flags;
    forced_depth_ = 1;
}

void Context::endForced() {
    if (!forced_depth_) return;
    in_ = forced_saved_in_;
    forced_ = 0;
    forced_depth_ = 0;
}

} // namespace ui
