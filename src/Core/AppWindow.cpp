#include "AppWindow.hpp"
#include "Utils.hpp"
#include "../UI/Theme.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

bool AppWindow::create(const char* title, int cols, int rows, float fontPx,
                       GLFWwindow* share) {
    title_ = title ? title : "ConUI";
    font_px_ = fontPx;

    // The frame must stay native, so the window is NOT undecorated: the styles
    // are what snap, the shadow and the minimise animation are built on. Only
    // the caption's painting is taken over, by Platform (§8.1).
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    cell_w_ = (int)std::lround(0.600f * font_px_);
    cell_h_ = (int)std::lround(1.320f * font_px_);

    win_ = glfwCreateWindow(cols * cell_w_, rows * cell_h_, title_.c_str(), nullptr, share);
    if (!win_) return false;

    // Each window carries its own scale: two monitors can disagree about DPI,
    // and the cell size follows the scale.
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(win_, &sx, &sy);
    scale_ = sx > 0.0f ? sx : 1.0f;

    platform::InstallCustomFrame(win_, chrome::CaptionHeightPx(cell_h_));
    {
        // GLFW sized the window for a standard caption and frame; the custom
        // frame folded those into the client area, so the content came out
        // larger than asked by exactly that much. Put the asked size back.
        int x = 0, y = 0;
        glfwGetWindowPos(win_, &x, &y);
        platform::PlaceWindow(win_, x, y, cols * cell_w_, rows * cell_h_);
    }
    cells_.resize(cols, rows, BG_WINDOW);
    publishCaptionHoles();
    return true;
}

void AppWindow::destroy() {
    if (!win_) return;
    platform::Forget(win_);
    glfwDestroyWindow(win_);
    win_ = nullptr;
}

bool AppWindow::shouldClose() const {
    return !win_ || glfwWindowShouldClose(win_) == GLFW_TRUE;
}

void AppWindow::requestClose() {
    if (win_) glfwSetWindowShouldClose(win_, GLFW_TRUE);
}

bool AppWindow::focused() const {
    return win_ && glfwGetWindowAttrib(win_, GLFW_FOCUSED) == GLFW_TRUE;
}

void AppWindow::beginFrame() {
    if (!win_) return;
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(win_, &fbw, &fbh);
    if (fbw <= 0 || fbh <= 0) return;

    // The grid is whole cells; whatever does not divide evenly is left to the
    // window background along the right and bottom edge, exactly as a terminal
    // does it (§8.1). Resizing stays pixel-smooth, the grid just steps.
    const int cols = fbw / cell_w_;
    // Fullscreen keeps half a cell free under the last row, so the status bar
    // does not sit on the monitor's very edge; the grid is shifted down by
    // whatever else is left over, so that half cell is exact.
    const int pad = platform::IsFullscreen(win_) ? cell_h_ / 2 : 0;
    const int rows = std::max(1, (fbh - pad) / cell_h_);
    grid_oy_ = pad > 0 ? std::max(0, fbh - pad - rows * cell_h_) : 0;
    spill_x_ = fbw - cols * cell_w_;
    spill_y_ = fbh - rows * cell_h_ - grid_oy_;

    if (cols != cells_.width() || rows != cells_.height()) {
        cells_.resize(cols < 1 ? 1 : cols, rows < 1 ? 1 : rows, BG_WINDOW);
        publishCaptionHoles();
    } else {
        cells_.clear(BG_WINDOW);
    }
}

void AppWindow::pointer(double& px, double& py) const {
    px = py = -1.0;
    if (!win_) return;
    glfwGetCursorPos(win_, &px, &py);
    // Cursor positions are in screen coordinates; the cell grid is in
    // framebuffer pixels, and on a HiDPI display those differ.
    int ww = 0, wh = 0, fw = 0, fh = 0;
    glfwGetWindowSize(win_, &ww, &wh);
    glfwGetFramebufferSize(win_, &fw, &fh);
    if (ww > 0 && wh > 0) {
        px *= (double)fw / ww;
        py *= (double)fh / wh;
    }
}

void AppWindow::publishCaptionHoles() {
    chrome::ButtonSpan spans[chrome::BTN_COUNT];
    chrome::ButtonSpans(cells_.width(), cell_w_, cell_h_, spans);
    int x0[chrome::BTN_COUNT], x1[chrome::BTN_COUNT];
    for (int i = 0; i < chrome::BTN_COUNT; ++i) { x0[i] = spans[i].x0; x1[i] = spans[i].x1; }
    // Without these the OS treats the whole caption row as "drag the window"
    // and the caption boxes never see a click.
    platform::SetCaptionHoles(win_, x0, x1, chrome::BTN_COUNT);
}

void AppWindow::pumpCaptionInput() {
    if (!win_) return;
    if (platform::IsFullscreen(win_)) {
        // No caption in fullscreen: nothing is drawn there, so
        // nothing should hover or latch a press either.
        hover_ = chrome::BTN_NONE;
        pressed_ = chrome::BTN_NONE;
        mouse_was_down_ = glfwGetMouseButton(win_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        return;
    }

    double px = 0, py = 0;
    pointer(px, py);
    hover_ = focused() ? chrome::HitTest((int)px, (int)py, cells_.width(), cell_w_, cell_h_)
                       : chrome::BTN_NONE;

    const bool down = glfwGetMouseButton(win_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (down && !mouse_was_down_) {
        pressed_ = hover_;                       // press latches the box
    } else if (!down && mouse_was_down_) {
        // A click counts only if it is released over the box it started on —
        // the same rule every other button in the system will follow (§9).
        if (pressed_ != chrome::BTN_NONE && pressed_ == hover_) {
            switch (pressed_) {
            case chrome::BTN_CLOSE:      requestClose(); break;
            case chrome::BTN_COLLAPSE:   glfwIconifyWindow(win_); break;
            case chrome::BTN_ZOOM:
                if (platform::IsMaximized(this->win_)) glfwRestoreWindow(win_);
                else                                   glfwMaximizeWindow(win_);
                break;
            case chrome::BTN_FULLSCREEN: platform::ToggleFullscreen(win_); break;
            default: break;
            }
        }
        pressed_ = chrome::BTN_NONE;
    }
    mouse_was_down_ = down;
}

void AppWindow::drawCaption(const chrome::CaptionIcons* icons) {
    if (platform::IsFullscreen(win_)) return;   // no title bar in fullscreen
    chrome::DrawCaption(cells_, title_.c_str(), focused(), cell_w_, cell_h_,
                        hover_, pressed_, spill_x_, icons);
}

void AppWindow::pumpUiInput() {
    if (!win_) return;

    // Edges, not levels. A widget must fire once per click, so "pressed" and
    // "released" are computed here from the transition; every window keeps its
    // own copy, or two windows would see each other's clicks.
    double px = 0, py = 0;
    pointer(px, py);
    ui_in_.mouse_x = cell_w_ > 0 ? (float)(px / cell_w_) : -1.0f;
    ui_in_.mouse_y = cell_h_ > 0 ? (float)((py - grid_oy_) / cell_h_) : -1.0f;
    ui_in_.cell_w = cell_w_;
    ui_in_.cell_h = cell_h_;

    const double now = glfwGetTime();
    ui_in_.time = now;

    const bool down = focused() &&
                      glfwGetMouseButton(win_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    ui_in_.mouse_pressed  = down && !ui_in_.mouse_down;
    ui_in_.mouse_released = !down && ui_in_.mouse_down;
    ui_in_.mouse_down     = down;

    const bool rdown = focused() &&
                       glfwGetMouseButton(win_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    ui_in_.mouse_right_pressed = rdown && !right_was_down_;
    right_was_down_ = rdown;

    // Double click: a second press soon after the first, in the same cell.
    // GLFW does not report it, so it is derived here, per window.
    ui_in_.mouse_double = false;
    if (ui_in_.mouse_pressed) {
        const bool near = std::fabs(ui_in_.mouse_x - last_click_x_) < 1.0f &&
                          std::fabs(ui_in_.mouse_y - last_click_y_) < 1.0f;
        ui_in_.mouse_double = near && now - last_click_t_ < kDoubleClickSec;
        last_click_t_ = ui_in_.mouse_double ? -1.0 : now;   // a third press starts over
        last_click_x_ = ui_in_.mouse_x;
        last_click_y_ = ui_in_.mouse_y;
    }

    ui_in_.wheel = wheel_;
    wheel_ = 0.0f;

    // Edges, plus auto-repeat for the keys that step a value or walk a
    // list: holding → on a slider must keep moving it.
    static const int kKeys[K_COUNT] = {
        GLFW_KEY_TAB, GLFW_KEY_ENTER, GLFW_KEY_KP_ENTER, GLFW_KEY_SPACE, GLFW_KEY_ESCAPE,
        GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN,
        GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN, GLFW_KEY_HOME, GLFW_KEY_END,
        GLFW_KEY_BACKSPACE, GLFW_KEY_DELETE, GLFW_KEY_INSERT,
        GLFW_KEY_A, GLFW_KEY_C, GLFW_KEY_X, GLFW_KEY_V, GLFW_KEY_F11, GLFW_KEY_S,
        GLFW_KEY_F1, GLFW_KEY_Q, GLFW_KEY_F2,
    };
    static const bool kRepeats[K_COUNT] = {
        true, false, false, false, false,
        true, true, true, true,
        true, true, false, false,
        true, true, false,
        false, false, false, true, false, false,
        false, false, false,
    };
    bool edge[K_COUNT] = {};
    for (int i = 0; i < K_COUNT; ++i) {
        const bool held = focused() && glfwGetKey(win_, kKeys[i]) == GLFW_PRESS;
        if (held && !prev_keys_[i]) {
            edge[i] = true;
            repeat_at_[i] = now + kRepeatDelay;
        } else if (held && kRepeats[i] && now >= repeat_at_[i]) {
            edge[i] = true;
            repeat_at_[i] = now + kRepeatRate;
        }
        prev_keys_[i] = held;
    }
    ui_in_.key_tab    = edge[K_TAB];
    ui_in_.key_enter  = edge[K_ENTER] || edge[K_KP_ENTER];
    ui_in_.key_space  = edge[K_SPACE];
    ui_in_.key_escape = edge[K_ESC];
    ui_in_.key_left   = edge[K_LEFT];
    ui_in_.key_right  = edge[K_RIGHT];
    ui_in_.key_up     = edge[K_UP];
    ui_in_.key_down   = edge[K_DOWN];
    ui_in_.key_pgup   = edge[K_PGUP];
    ui_in_.key_pgdn   = edge[K_PGDN];
    ui_in_.key_home   = edge[K_HOME];
    ui_in_.key_end    = edge[K_END];

    auto held = [&](int k) { return focused() && glfwGetKey(win_, k) == GLFW_PRESS; };
    ui_in_.key_shift = held(GLFW_KEY_LEFT_SHIFT)   || held(GLFW_KEY_RIGHT_SHIFT);
    ui_in_.key_ctrl  = held(GLFW_KEY_LEFT_CONTROL) || held(GLFW_KEY_RIGHT_CONTROL);

    ui_in_.key_backspace = edge[K_BACKSPACE];
    ui_in_.key_delete    = edge[K_DELETE];
    ui_in_.key_insert    = edge[K_INSERT];
    ui_in_.key_f11       = edge[K_F11];
    // The chords. GLFW key tokens name physical positions, so Ctrl+C is the
    // same key on a Russian layout as on an English one — which is exactly
    // what people expect of Ctrl+C.
    const bool ctrl = ui_in_.key_ctrl;
    ui_in_.key_select_all = ctrl && edge[K_A];
    ui_in_.key_copy       = ctrl && edge[K_C];
    ui_in_.key_cut        = ctrl && edge[K_X];
    ui_in_.key_paste      = ctrl && edge[K_V];
    ui_in_.key_save       = ctrl && edge[K_S];
    ui_in_.key_quit       = ctrl && edge[K_Q];
    ui_in_.key_f1         = edge[K_F1];
    ui_in_.key_f2         = edge[K_F2];
    ui_in_.paste.clear();
    if (ui_in_.key_paste) {
        // Read only on the chord: the clipboard is the OS's, and polling it
        // every frame would be both wasteful and rude.
        if (const char* s = glfwGetClipboardString(win_)) ui_in_.paste = s;
    }

    ui_in_.text.swap(typed_);
    typed_.clear();
}

void AppWindow::applyClipboard(const ui::Context& ctx) {
    if (win_ && ctx.clipboardSet()) glfwSetClipboardString(win_, ctx.clipboardOut().c_str());
}

void AppWindow::applyCursor(ui::Cursor c) {
    if (!win_ || c == cursor_) return;
    cursor_ = c;
    // Standard cursors are owned by GLFW's shared pool; create once, reuse.
    static GLFWcursor* hres = nullptr;
    static GLFWcursor* vres = nullptr;
    static GLFWcursor* ibeam = nullptr;
    static GLFWcursor* hand = nullptr;
    GLFWcursor* use = nullptr;
    if (c == ui::Cursor::ResizeH) {
        if (!hres) hres = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
        use = hres;
    } else if (c == ui::Cursor::ResizeV) {
        if (!vres) vres = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
        use = vres;
    } else if (c == ui::Cursor::IBeam) {
        if (!ibeam) ibeam = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
        use = ibeam;
    } else if (c == ui::Cursor::Pointer) {
        if (!hand) hand = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
        use = hand;
    } else if (c == ui::Cursor::Move) {
#ifdef GLFW_RESIZE_ALL_CURSOR      // GLFW 3.4+; older ones have no four-way arrow
        static GLFWcursor* move = nullptr;
        if (!move) move = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
        use = move;
#else
        if (!hand) hand = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
        use = hand;
#endif
    }
    glfwSetCursor(win_, use);   // nullptr = the arrow
}

void AppWindow::setFontPx(float px) {
    if (px <= 0.0f || px == font_px_) return;
    font_px_ = px;
    cell_w_ = (int)std::lround(0.600f * font_px_);
    cell_h_ = (int)std::lround(1.320f * font_px_);
    if (!win_) return;
    platform::SetCaptionHeight(win_, chrome::CaptionHeightPx(cell_h_));
    // Force the grid to be rebuilt at the new cell size, and the caption's
    // click-through holes to move with it.
    cells_.resize(0, 0, BG_WINDOW);
    beginFrame();
    publishCaptionHoles();
}

void AppWindow::saveGeometry(WindowGeometry& g) const {
    if (!win_) return;
    // Fullscreen is not a state to restore into, and a window that went
    // fullscreen from maximised is no longer IsZoomed() — keep whatever was
    // saved before it, maximised flag included.
    if (platform::IsFullscreen(win_)) return;
    const bool maxed = platform::IsMaximized(win_);
    g.maximized = maxed ? 1 : 0;
    if (maxed || glfwGetWindowAttrib(win_, GLFW_ICONIFIED) == GLFW_TRUE)
        return;                                  // keep the last normal rectangle
    int x = 0, y = 0, w = 0, h = 0;
    glfwGetWindowPos(win_, &x, &y);
    glfwGetWindowSize(win_, &w, &h);
    if (w <= 0 || h <= 0) return;
    g.x = (float)x; g.y = (float)y; g.w = (float)w; g.h = (float)h;
}

void AppWindow::applyGeometry(const WindowGeometry& g) {
    if (!win_) return;
    // w <= 0 means "never saved" (the defaults are -1). Anything absurd — a
    // damaged file, a NaN — is ignored rather than trusted.
    auto sane = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    if (sane(g.w, 160.0f, 20000.0f) && sane(g.h, 120.0f, 20000.0f)) {
        int x = 0, y = 0;
        glfwGetWindowPos(win_, &x, &y);
        // Negative positions are legitimate: a monitor left of the primary.
        if (sane(g.x, -20000.0f, 20000.0f) && sane(g.y, -20000.0f, 20000.0f)) {
            x = (int)g.x;
            y = (int)g.y;
        }
        platform::PlaceWindow(win_, x, y, (int)g.w, (int)g.h);
    }
    platform::ClampToWorkArea(win_);
    if (g.maximized) glfwMaximizeWindow(win_);
}
