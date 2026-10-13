#pragma once
#include "Platform.hpp"
#include "../Render/CellBuffer.hpp"
#include "../UI/Context.hpp"
#include "../UI/WindowChrome.hpp"
#include "Utils.hpp"

#include <string>
#include <vector>

struct GLFWwindow;
class FontAtlas;

// ---------------------------------------------------------------------------
// One real OS window: a GLFW window, its own CellBuffer, its own cell size,
// its own caption state.
//
// Multi-window is built in from the start rather than bolted on when the theme
// editor needs it: retrofitting a single-window loop means rewriting both the
// loop and every path that input takes. The GL context and the glyph atlas are
// shared — a second window is a second surface, not a second renderer.
//
// Each window keeps its OWN content scale, because two monitors can disagree
// about DPI. The cell size follows from that scale, so the grid is per-window
// too, and the atlas is asked for the size this window needs.
// ---------------------------------------------------------------------------

class AppWindow {
public:
    bool create(const char* title, int cols, int rows, float fontPx, GLFWwindow* share);
    void destroy();

    /** Recomputes the grid from the framebuffer size and clears the buffer. */
    void beginFrame();

    bool shouldClose() const;
    void requestClose();

    GLFWwindow* handle() const { return win_; }
    CellBuffer& cells() { return cells_; }
    const CellBuffer& cells() const { return cells_; }

    int cellW() const { return cell_w_; }
    int cellH() const { return cell_h_; }
    int cols()  const { return cells_.width(); }
    int rows()  const { return cells_.height(); }
    float fontPx() const { return font_px_; }
    /**
     * Changes the cell size at run time (density, §3). The window keeps its
     * pixel size; the grid is recomputed from it on the next beginFrame(),
     * and the caption height follows the new cell.
     */
    void setFontPx(float px);

    /**
     * The window's place for data.bin. A maximised, fullscreen or minimised
     * window keeps the LAST NORMAL rectangle (plus the maximised flag), so
     * restoring it later lands where the user last had it.
     */
    void saveGeometry(WindowGeometry& g) const;
    /**
     * Puts the window back. Values are checked, and the result is pulled into
     * the work area of the nearest monitor — the one it was on may be gone.
     */
    void applyGeometry(const WindowGeometry& g);

    bool focused() const;
    const std::string& title() const { return title_; }

    /**
     * Draws the caption. `icons` are the raster box icons for this window's
     * cell size (null: the boxes fall back to geometry).
     */
    void drawCaption(const chrome::CaptionIcons* icons = nullptr);

    /** Pointer state, in client pixels. */
    void  pointer(double& px, double& py) const;
    int   hoveredButton() const { return hover_; }

    /** Call once per frame before drawing: updates hover/press and acts on clicks. */
    void pumpCaptionInput();

    /**
     * This frame's input for the immediate-mode context, with the pointer
     * already converted from pixels to CELLS. Edge flags (pressed / released /
     * key hits) are computed here because they are per-window: two windows
     * must not see each other's clicks.
     */
    const ui::Input& uiInput() const { return ui_in_; }
    /** Collects the frame's input. Call before Context::begin(). */
    void pumpUiInput();

    /** Text typed since the last frame, fed by the GLFW char callback. */
    void pushChar(uint32_t cp) { typed_.push_back(cp); }
    /** Wheel notches since the last frame, fed by the GLFW scroll callback. */
    void pushScroll(float dy) { wheel_ += dy; }
    /** Sets the pointer shape a widget asked for (splitter ↔ / ↕). */
    void applyCursor(ui::Cursor c);
    /** Hands whatever a widget copied this frame to the OS clipboard. */
    void applyClipboard(const ui::Context& ctx);

    /** The leftover pixels below/right of the last whole cell (§8.1). */
    int spillX() const { return spill_x_; }
    int spillY() const { return spill_y_; }
    /**
     * How far down the grid starts, in pixels. 0 in a window; in fullscreen
     * the grid is shifted so exactly half a cell stays free under the last
     * row (the status bar), and the rest of the leftover goes on top.
     */
    int gridOffsetY() const { return grid_oy_; }

private:
    GLFWwindow* win_ = nullptr;
    CellBuffer  cells_;
    std::string title_;
    float font_px_ = 15.0f;
    float scale_   = 1.0f;
    int   cell_w_ = 9, cell_h_ = 20;
    int   spill_x_ = 0, spill_y_ = 0;
    int   grid_oy_ = 0;

    int  hover_ = chrome::BTN_NONE;
    int  pressed_ = chrome::BTN_NONE;
    bool mouse_was_down_ = false;

    ui::Input ui_in_;
    std::vector<uint32_t> typed_;
    float wheel_ = 0.0f;
    bool  right_was_down_ = false;

    enum KeyEdge {
        K_TAB = 0, K_ENTER, K_KP_ENTER, K_SPACE, K_ESC,
        K_LEFT, K_RIGHT, K_UP, K_DOWN, K_PGUP, K_PGDN, K_HOME, K_END,
        K_BACKSPACE, K_DELETE, K_INSERT, K_A, K_C, K_X, K_V, K_F11, K_S, K_F1, K_Q, K_F2, K_COUNT
    };
    bool   prev_keys_[K_COUNT] = {};   // for edge detection
    double repeat_at_[K_COUNT] = {};   // next auto-repeat time per key
    static constexpr double kRepeatDelay = 0.40;   // s before the first repeat
    static constexpr double kRepeatRate  = 0.035;  // s between repeats

    static constexpr double kDoubleClickSec = 0.40;
    double last_click_t_ = -1.0;
    float  last_click_x_ = -9.0f, last_click_y_ = -9.0f;

    ui::Cursor cursor_ = ui::Cursor::Arrow;

    void publishCaptionHoles();
};
