#pragma once
#include "Strings.hpp"
#include "Theme.hpp"

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

class CellBuffer;

// ---------------------------------------------------------------------------
// The immediate-mode core (docs/DESIGN_SYSTEM.md §9).
//
//     if (ui::Button(ctx, str::ID_RUN)) { ... }
//
// Familiar from ImGui, but written
// here, because the whole point of ConUI is that the drawing model is a cell
// grid rather than a vertex soup.
//
// What this file owns:
//   * IDENTITY — a hash stack, so two buttons with the same label in two
//     panels are still two buttons;
//   * STATE that must survive between frames although the widgets do not:
//     which item the pointer is over, which is held down, which has keyboard
//     focus, and a small per-id store for things like a scroll offset;
//   * LAYOUT — a cursor in CELLS with a stack of regions. A region is a clip
//     rectangle plus a layout origin, and the two differ on purpose: a
//     scroll region's origin sits above its clip by the scroll offset;
//   * INPUT routing, including the rule that a widget only claims the mouse
//     if nothing above it already did.
//
// The widgets themselves live in UI/Widgets/ and are free functions
// over a Context, so adding one never touches this class.
// ---------------------------------------------------------------------------

namespace ui {

using Id = uint64_t;
constexpr Id kNoId = 0;

/** A 32-bit integer hash: the glitches' noise, repeatable from a seed (§9). */
inline uint32_t Mix32(uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/** What the pointer and keyboard did this frame, in CELL coordinates. */
struct Input {
    float mouse_x = -1.0f, mouse_y = -1.0f;   // cells, fractional
    // The cell in PIXELS, for the few shapes that must be an exact number of
    // pixels thick whatever the density (the splitter's 2 px pill, §8.17).
    int   cell_w = 9, cell_h = 20;
    bool  mouse_down = false;                  // left button, current state
    bool  mouse_pressed = false;               // went down this frame
    bool  mouse_released = false;              // came up this frame
    bool  mouse_double = false;                // second press within the OS interval
    bool  mouse_right_pressed = false;         // right button went down: context menus
    float wheel = 0.0f;                        // notches, positive = away from the user

    bool  key_tab = false, key_shift = false, key_ctrl = false;
    bool  key_enter = false, key_space = false, key_escape = false;
    bool  key_left = false, key_right = false, key_up = false, key_down = false;
    bool  key_backspace = false, key_delete = false, key_insert = false;
    bool  key_f11 = false;
    // Editing chords, resolved by the window from Ctrl+A/C/X/V. They are keys,
    // not text: the char callback never reports them.
    bool  key_select_all = false, key_copy = false, key_cut = false, key_paste = false;
    bool  key_save = false;                    // Ctrl+S
    bool  key_quit = false;                    // Ctrl+Q
    bool  key_f1 = false;
    bool  key_f2 = false;                      // edit the current cell (§8.19)
    std::string paste;                         // clipboard text, filled only on key_paste
    bool  key_pgup = false, key_pgdn = false, key_home = false, key_end = false;

    double time = 0.0;                         // seconds, for the two blinks (§9)

    std::vector<uint32_t> text;                // UTF-32, from the char callback
};

/** A rectangle in cells. Half-open: x .. x+w-1. */
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= (float)x && py >= (float)y &&
               px <  (float)(x + w) && py <  (float)(y + h);
    }
    Rect inset(int d) const { return Rect{ x + d, y + d, w - 2 * d, h - 2 * d }; }
    bool empty() const { return w <= 0 || h <= 0; }
};

/**
 * States a showcase can force on the widgets inside Context::beginForced():
 * the playground's STATES screen draws every widget in every state side by
 * side, and each widget draws itself — there is no second "demo" renderer.
 */
enum ForceFlags : int {
    FORCE_NONE    = 0,
    FORCE_HOT     = 1 << 0,
    FORCE_PRESSED = 1 << 1,
    FORCE_FOCUS   = 1 << 2,
};

/** The pointer shape a widget asks for; the window applies it after the frame. */
// Move: a picture that is moved by dragging (§8.21).
enum class Cursor : uint8_t { Arrow = 0, ResizeH, ResizeV, IBeam, Pointer, Move };

class Context {
public:
    /** Starts a frame against a buffer. `in` is consumed, not copied back. */
    void begin(CellBuffer& cells, const Input& in);
    void end();

    // ---- identity ------------------------------------------------------
    void pushId(const char* s);
    void pushId(int i);
    void popId();
    Id   makeId(const char* s) const;
    Id   makeId(int i) const;

    // ---- layout --------------------------------------------------------
    /**
     * Constrains everything that follows to `r` — layout, input AND drawing —
     * until popRegion(). The clip is `r` cut to the parent; the layout origin
     * is `r` itself, uncut.
     */
    void pushRegion(const Rect& r);
    /**
     * Same, but the layout origin is `origin` rather than the clip's corner.
     * This is the whole of a scroll region: clip where the panel is, lay out
     * where the content would be if the panel were infinitely tall.
     */
    void pushRegion(const Rect& clip, int originX, int originY, int layoutW = -1);
    void popRegion();
    /** The current CLIP rectangle. */
    const Rect& region() const { return regions_.back().clip; }
    /** Width available to layout in the current region, and its left edge. */
    int  regionWidth() const { return regions_.back().layout_w; }
    int  regionLeft() const { return regions_.back().ox; }
    /** Lowest row any allocation in the current region reached, exclusive. */
    int  contentBottom() const { return regions_.back().max_y; }

    /** Reserves `w` x `h` cells at the cursor and returns where they landed. */
    Rect allocate(int w, int h);
    /** Reserves the rest of the current line, `h` rows tall. */
    Rect allocateRow(int h = 1);
    void newline(int rows = 1);
    void spacer(int cells);
    /** Puts the cursor at a column relative to the current region. */
    void column(int x);

    int  cursorX() const { return cur_x_; }
    int  cursorY() const { return cur_y_; }

    // ---- interaction ---------------------------------------------------
    /**
     * The one routing primitive every widget is built on. Returns true on a
     * completed click: pressed and released over the same item. A disabled
     * scope makes it inert.
     */
    bool interact(Id id, const Rect& r);
    /**
     * The same, for a target that is not whole cells: `r` is the cells the
     * item occupies (the clip and the focus ring go by it), `hx,hy,hw,hh` is
     * the box the POINTER is tested against, in cell fractions. A sidebar
     * icon is a row tall in the middle of a two-row slot, and the pointer
     * should answer to the icon, not to the slot around it.
     */
    bool interact(Id id, const Rect& r, float hx, float hy, float hw, float hh);

    bool isHovered(Id id) const { return !forced_depth_ && hovered_ == id && id != kNoId; }
    /** Hovered as far as DRAWING is concerned — last frame's answer, or this one's. */
    bool isHot(Id id) const {
        if (forced_depth_) return (forced_ & FORCE_HOT) != 0;
        return id != kNoId && (hovered_prev_ == id || hovered_ == id);
    }
    bool isActive(Id id)  const {
        if (forced_depth_) return (forced_ & FORCE_PRESSED) != 0;
        return active_ == id && id != kNoId;
    }
    /**
     * Focused AND the keyboard belongs to the content: while a menu or a dropdown list is
     * open (it claimed the keys last frame), the content under it is not
     * focused as far as keys go — or Enter in a menu would also press the
     * button that has the focus behind it.
     */
    bool isFocused(Id id) const {
        if (forced_depth_) return (forced_ & FORCE_FOCUS) != 0;
        return focus_ == id && id != kNoId && !(keys_claimed_prev_ && overlay_depth_ == 0);
    }
    /** Held down AND the pointer is still over it — a button slid off is not pressed. */
    bool isPressed(Id id) const {
        if (forced_depth_) return (forced_ & FORCE_PRESSED) != 0;
        return isActive(id) && active_over_;
    }
    /** Became active this very frame (the press that starts a drag). */
    bool justPressed(Id id) const { return !forced_depth_ && isActive(id) && in_.mouse_pressed; }

    /** Registers an item in the Tab order. Call before interact(). */
    void focusable(Id id);
    /** True when the focused item should act, i.e. Enter or Space was hit. */
    bool focusActivated(Id id) const;
    /** Moves keyboard focus to `id` (a click does this for you). */
    void setFocus(Id id) { focus_ = id; }
    /**
     * The item that should get the focus when a modal opens — the default
     * action (BTN_DEFAULT). In a destructive confirmation that is CANCEL: an
     * Enter pressed out of habit must not delete anything.
     */
    void preferFocus(Id id) { if (modal_now_ && overlay_depth_ > 0) modal_preferred_ = id; }

    /**
     * A disabled scope: widgets inside draw muted, take no input and drop out
     * of the Tab order (§9). Scopes nest.
     */
    /**
     * Everything until endForced() answers isHot/isPressed/isFocused with
     * `flags`, and takes no input at all — it is a picture of a state, not a
     * control. Does not nest.
     */
    void beginForced(int flags);
    void endForced();
    /** Inside beginForced(): a showcase picture of a state (the States screen). */
    bool forced() const { return forced_depth_ != 0; }

    // ---- glitch effects (§9) ------------------------------------------------
    /** The user's switch: with it off, no glitch of any kind plays. */
    void setGlitches(bool on) { glitches_ = on; }
    bool glitches() const { return glitches_; }

    enum class ScreenGlitch : uint8_t {
        Tear,          // the whole window tears: overlays included (a fault)
        UnderOverlay,  // only what lies under the overlays jolts (a danger modal opening)
    };
    /**
     * A whole-window glitch: 2–4 horizontal bands of rows shift sideways by
     * 1–4 cells, in three 70 ms steps, applied at end() to everything drawn
     * this frame. At most one every 2 s, never in a showcase, never with
     * glitches off. Returns true if it started.
     */
    bool glitchScreen(ScreenGlitch kind);

    void beginDisabled(bool disabled = true);
    void endDisabled();
    bool disabled() const { return disabled_depth_ > 0; }

    // ---- per-widget memory ---------------------------------------------
    /**
     * A float that survives between frames, keyed by id. Immediate mode
     * forgets widgets every frame; this is for the few things that must not
     * be forgotten and do not belong to the caller — where a drag grabbed
     * the thumb, a scroll offset, which end of a range is being moved.
     */
    float& state(Id id, float init = 0.0f);

    /**
     * The same idea for a widget whose memory is a struct — a text field's
     * cursor, selection, scroll and history position. One id, one type: a
     * widget always asks for its own id with its own struct.
     */
    template <class T> T& blob(Id id) {
        auto it = blobs_.find(id);
        if (it == blobs_.end())
            it = blobs_.emplace(id, std::unique_ptr<BlobBase>(new Blob<T>())).first;
        return static_cast<Blob<T>*>(it->second.get())->value;
    }

    // ---- overlays (tooltip, menus, dropdown list, modal) -------------------
    /**
     * Starts drawing above the window's content: the overlay cell grid and
     * the Z_OVERLAY band, a region that escapes every parent clip, and an
     * overlay level one deeper than the current one.
     *
     * A `blocking` overlay claims its rectangle for input: from the next
     * frame on, anything at a SHALLOWER level under that rectangle no longer
     * sees the pointer — the same one-frame-behind rule as hover, and the
     * reason the order of calls does not matter. A tooltip is not blocking:
     * it must not steal the hover that keeps it on screen.
     */
    void beginOverlay(const Rect& r, bool blocking = true);
    void endOverlay();

    /**
     * A modal overlay: while it was open last frame, everything below it is
     * inert — pointer, wheel and keyboard focus — the Tab order is trapped
     * inside it, and the content is dimmed (§8.9). Call instead of
     * beginOverlay()/endOverlay().
     */
    void beginModal(const Rect& r);
    void endModal();

    /** True when the pointer at (x, y) is under an overlay above this level. */
    bool blockedAt(float x, float y) const;

    /**
     * "Draw me again, in at most `seconds`." Anything that moves by itself —
     * a blink, a travelling highlight, a glitch, a live chart — says so every
     * frame; the loop then sleeps until the soonest of them instead of
     * redrawing sixty times a second at rest (§9). The common ones say it
     * for you: blinkOn(), the busy bar, the glitches.
     */
    void animate(double seconds = 0.0) const {
        if (seconds < 0.0) seconds = 0.0;
        if (next_frame_ < 0.0 || seconds < next_frame_) next_frame_ = seconds;
    }
    /** Seconds until the frame this one asked for; negative when it asked for none. */
    double nextFrameIn() const { return next_frame_; }

    /** The last item that went through interact() — tooltips attach to it. */
    Id   lastId() const { return last_id_; }
    /** How long `id` has been under the pointer, seconds; 0 if it is not. */
    double hoverSeconds(Id id) const;

    /** An open popup or a text field keeps Esc for itself (the app would quit). */
    void claimEscape() { if (!forced_depth_) escape_claimed_ = true; }
    /** An open popup takes the keyboard from the content, from the next frame. */
    void claimKeys() { if (!forced_depth_) { keys_claimed_now_ = true; escape_claimed_ = true; } }
    bool escapeClaimed() const { return escape_claimed_ || text_captured_; }

    // ---- text and the clipboard ------------------------------------------
    /**
     * A focused text widget claims the keyboard for the frame. The app then
     * leaves Esc, letters and Space alone — without this, Esc in a text field
     * would quit the application.
     */
    void captureText() { if (!forced_depth_) text_captured_ = true; }
    bool textCaptured() const { return text_captured_; }
    /**
     * Keeps this frame's Tab from moving the focus: a cell being edited uses
     * Tab to commit and step to the next cell (§8.19). Only while editing —
     * a widget that always ate Tab would be a keyboard trap.
     */
    void consumeTab() { if (!forced_depth_) want_focus_next_ = want_focus_prev_ = false; }
    /** Text a widget copied this frame; the window hands it to the OS. */
    void setClipboard(const std::string& s) { clipboard_out_ = s; clipboard_set_ = true; }
    bool clipboardSet() const { return clipboard_set_; }
    const std::string& clipboardOut() const { return clipboard_out_; }

    // ---- pointer and keyboard helpers ----------------------------------
    /** Takes the wheel: the first caller gets it, the rest see 0. */
    float takeWheel();
    float wheel() const { return in_.wheel; }
    void  setCursor(Cursor c) { cursor_ = c; }
    Cursor cursor() const { return cursor_; }
    /** The blink phase shared by the cursor and the focus marker (~530 ms, §9). */
    bool  blinkOn() const;

    /** Where the focused widget was drawn this frame, if it was. */
    bool  focusRect(Rect& out) const;
    /** True for the frames right after Tab moved the focus. */
    bool  focusMovedByKey() const { return focus_moved_by_key_; }
    /** How many focusables have been registered so far this frame. */
    int   focusableCount() const { return (int)focusables_.size(); }
    /** True if the focused id is among focusables [from, to). */
    bool  focusIn(int from, int to) const;

    /** Cells written by the context; the caller owns it. */
    CellBuffer& cells() { return *cells_; }
    const Input& input() const { return in_; }

    /** The id holding keyboard focus, or kNoId. */
    Id  focusedId() const { return focus_; }

private:
    struct Region {
        Rect clip;
        int  ox = 0, oy = 0;       // layout origin
        int  layout_w = 0;
        int  max_y = 0;            // content extent, for scroll regions
        int  saved_x = 0, saved_y = 0, saved_line_h = 1;   // parent's cursor
    };

    CellBuffer* cells_ = nullptr;
    Input       in_;

    std::vector<Id>     id_stack_;
    std::vector<Region> regions_;
    int cur_x_ = 0, cur_y_ = 0;
    int line_h_ = 1;

    Id hovered_ = kNoId;        // resolved during the frame
    Id hovered_prev_ = kNoId;   // what the widgets see, from last frame's pass
    Id active_ = kNoId;
    bool active_over_ = false;  // pointer over the active widget this frame
    Id focus_ = kNoId;

    // Tab order, rebuilt every frame: widgets come and go, so the order is
    // whatever was drawn, in draw order.
    std::vector<Id> focusables_;
    bool want_focus_next_ = false;
    bool want_focus_prev_ = false;
    int  focus_moved_by_key_ = 0;   // frames left to report it
    bool focus_rect_valid_ = false;
    Rect focus_rect_;

    int  disabled_depth_ = 0;
    int  forced_ = 0, forced_depth_ = 0;

    bool   glitches_ = true;
    double sg_start_ = -1.0;
    ScreenGlitch sg_kind_ = ScreenGlitch::Tear;
    uint32_t sg_seed_ = 0;
    void applyScreenGlitch();
    Input forced_saved_in_;        // the real input, parked while forced
    std::vector<int> disabled_stack_;
    bool wheel_taken_ = false;
    Cursor cursor_ = Cursor::Arrow;

    std::unordered_map<Id, float> store_;

    struct BlobBase { virtual ~BlobBase() = default; };
    template <class T> struct Blob : BlobBase { T value{}; };
    std::unordered_map<Id, std::unique_ptr<BlobBase>> blobs_;

    bool text_captured_ = false;
    bool escape_claimed_ = false;
    bool keys_claimed_now_ = false, keys_claimed_prev_ = false;

    struct OverlayRect { Rect r; int level; };
    std::vector<OverlayRect> overlay_prev_, overlay_now_;
    int  overlay_depth_ = 0;
    struct OverlaySave { int saved_band; };
    std::vector<OverlaySave> overlay_stack_;
    bool modal_prev_ = false, modal_now_ = false;
    int  modal_focus_from_ = -1, modal_focus_to_ = -1;
    Id   modal_preferred_ = kNoId;
    Id   last_id_ = kNoId;
    mutable double next_frame_ = -1.0;   // this frame's request for the next one
    Id   hover_tracked_ = kNoId;
    double hover_since_ = 0.0;
    bool clipboard_set_ = false;
    std::string clipboard_out_;

    void resolveTab();
    void pushRegionRaw(const Rect& clip, int originX, int originY, int layoutW, bool absolute);
};

} // namespace ui
