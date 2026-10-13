#include "Platform.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>

// windowsx.h defines IsMaximized/IsMinimized as macros, which would quietly
// rename platform::IsMaximized into platform::IsZoomed at the preprocessor
// stage and leave the link failing on a symbol nobody wrote.
#undef IsMaximized
#undef IsMinimized

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <cstdio>
#include <unordered_map>
#include <vector>

// Attributes that may be missing from an older SDK header. Values are the
// documented ones; naming them here keeps the build independent of which
// Windows SDK the toolchain happens to ship.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#define CONUI_DWMWCP_DONOTROUND 1
#define CONUI_DWMSBT_NONE       1
#define CONUI_DWMSBT_MICA       2   // DWMSBT_MAINWINDOW
#define CONUI_DWMSBT_ACRYLIC    3   // DWMSBT_TRANSIENTWINDOW

namespace {

struct Hole { int x0, x1; };

struct WinState {
    GLFWwindow* glfw   = nullptr;
    WNDPROC     orig   = nullptr;     // the proc chained to, never assumed about
    int         caption_px = 20;
    std::vector<Hole> holes;          // caption spans that stay clickable
    bool        frame_installed = false;
    bool        material_ok = false;
    platform::Material material = platform::Material::None;
    // saved placement for borderless fullscreen — the whole placement, not
    // just a rectangle, so a maximised window comes back maximised
    bool        fullscreen = false;
    WINDOWPLACEMENT saved_wp{};
    LONG_PTR    saved_style = 0;
};

std::unordered_map<HWND, WinState> g_windows;

WinState* find(HWND h) {
    auto it = g_windows.find(h);
    return it == g_windows.end() ? nullptr : &it->second;
}

HWND hwnd_of(GLFWwindow* w) { return w ? (HWND)glfwGetWin32Window(w) : nullptr; }

/**
 * The resize margin the OS keeps outside the visible border. This is what
 * makes the edges grabbable even though the client area covers the whole
 * window, and it is also exactly the amount a maximised custom-frame window
 * spills past the work area if nobody compensates.
 */
int frame_x() { return GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER); }
int frame_y() { return GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER); }

bool in_hole(const WinState& s, int x) {
    for (const Hole& h : s.holes)
        if (x >= h.x0 && x < h.x1) return true;
    return false;
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WinState* s = find(hwnd);
    if (!s) return DefWindowProcW(hwnd, msg, wp, lp);
    const WNDPROC orig = s->orig;

    switch (msg) {

    // ── the caption goes away, the frame stays ──────────────────────────────
    // Returning the request rectangle untouched makes the client area cover
    // the whole window, which is what removes the caption. The border, the
    // shadow and the rounded/square corners are drawn by DWM outside this
    // rectangle, so they survive — that is the whole trick.
    case WM_NCCALCSIZE: {
        if (!wp) break;
        NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)lp;

        // The known trap: a maximised window with a custom frame is sized to
        // the monitor PLUS the invisible resize margin, so without this the
        // top and sides of the content sit off-screen.
        if (IsZoomed(hwnd)) {
            const int fx = frame_x(), fy = frame_y();
            p->rgrc[0].left   += fx;
            p->rgrc[0].right  -= fx;
            p->rgrc[0].top    += fy;
            p->rgrc[0].bottom -= fy;

            // An auto-hiding taskbar only reveals itself when the maximised
            // window does not cover its last pixel, so give it one back.
            APPBARDATA abd{};
            abd.cbSize = sizeof abd;
            if (SHAppBarMessage(ABM_GETSTATE, &abd) & ABS_AUTOHIDE)
                p->rgrc[0].bottom -= 1;
        }
        return 0;
    }

    // ── where the window can be dragged and resized ─────────────────────────
    case WM_NCHITTEST: {
        const LRESULT def = CallWindowProcW(orig, hwnd, msg, wp, lp);
        if (def != HTCLIENT) return def;   // let the OS keep whatever it found

        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc{};
        GetWindowRect(hwnd, &rc);
        const int x = pt.x - rc.left, y = pt.y - rc.top;
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;

        // No edges to grab in fullscreen: the
        // window already covers the whole monitor, and letting the resize
        // margin through would let a drag near the edge quietly resize it.
        if (!IsZoomed(hwnd) && !s->fullscreen) {
            const int m = frame_x();      // same margin on every edge
            const bool L = x < m, R = x >= w - m, T = y < m, B = y >= h - m;
            if (T && L) return HTTOPLEFT;
            if (T && R) return HTTOPRIGHT;
            if (B && L) return HTBOTTOMLEFT;
            if (B && R) return HTBOTTOMRIGHT;
            if (L) return HTLEFT;
            if (R) return HTRIGHT;
            if (T) return HTTOP;
            if (B) return HTBOTTOM;
        }
        // The caption row drags the window — except over the caption boxes, which
        // must receive ordinary clicks, and except in fullscreen, where there
        // is no caption to drag by.
        //
        // In CLIENT pixels, not window ones: the caption band and the holes
        // in it are given by the app, which knows only its own cell
        // grid. The two are the same rectangle while the window is ordinary,
        // but a MAXIMISED window's client area is inset by the invisible
        // resize frame (WM_NCCALCSIZE above), and going by window
        // coordinates there put every hole a frame's width off the box it
        // belongs to.
        POINT cpt = pt;
        ScreenToClient(hwnd, &cpt);
        if (!s->fullscreen && cpt.y >= 0 && cpt.y < s->caption_px && !in_hole(*s, cpt.x))
            return HTCAPTION;
        return HTCLIENT;
    }

    // Alt+Space and right-click on the caption are lost with a custom frame;
    // putting the system menu back costs three lines, so it is put back.
    case WM_NCRBUTTONUP: {
        if (wp != HTCAPTION) break;
        HMENU m = GetSystemMenu(hwnd, FALSE);
        if (!m) break;
        const int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                            GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, hwnd, nullptr);
        if (cmd) PostMessageW(hwnd, WM_SYSCOMMAND, (WPARAM)cmd, 0);
        return 0;
    }

    case WM_DESTROY:
        // Unhook before the window goes, so nothing routes through a dead map.
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)orig);
        g_windows.erase(hwnd);
        return CallWindowProcW(orig, hwnd, msg, wp, lp);

    default:
        break;
    }
    return CallWindowProcW(orig, hwnd, msg, wp, lp);
}

void apply_square_corners(HWND h) {
    const DWORD pref = CONUI_DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);
}

} // namespace

namespace platform {

bool InstallCustomFrame(GLFWwindow* w, int captionPx) {
    HWND h = hwnd_of(w);
    if (!h) return false;
    if (find(h)) { SetCaptionHeight(w, captionPx); return true; }

    WinState s;
    s.glfw = w;
    s.caption_px = captionPx > 0 ? captionPx : 20;

    // The styles stay exactly as they were: WS_THICKFRAME, WS_CAPTION,
    // WS_MINIMIZEBOX and WS_MAXIMIZEBOX are what snap, Win+arrow and the
    // minimise animation are built on. Only the PAINTING of the caption goes.
    s.saved_style = GetWindowLongPtrW(h, GWL_STYLE);

    // GLFW does not surface window messages, so the proc is subclassed and the
    // original is called by the chain. Nothing here assumes anything about
    // what the original does — this is the most fragile spot in the whole
    // platform layer, and it is deliberately the smallest.
    s.orig = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
    if (!s.orig) return false;

    g_windows[h] = s;
    if (!SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)Proc)) {
        // Non-zero previous value is normal; a real failure sets last error.
        if (GetLastError() != 0) { g_windows.erase(h); return false; }
    }

    apply_square_corners(h);

    // Force a fresh WM_NCCALCSIZE so the caption disappears immediately
    // rather than at the first user resize.
    SetWindowPos(h, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    g_windows[h].frame_installed = true;
    return true;
}

void SetCaptionHeight(GLFWwindow* w, int captionPx) {
    if (WinState* s = find(hwnd_of(w))) s->caption_px = captionPx > 0 ? captionPx : 20;
}

void SetCaptionHoles(GLFWwindow* w, const int* x0, const int* x1, int count) {
    WinState* s = find(hwnd_of(w));
    if (!s) return;
    s->holes.clear();
    for (int i = 0; i < count; ++i) s->holes.push_back({ x0[i], x1[i] });
}

bool SetMaterial(GLFWwindow* w, Material m) {
    HWND h = hwnd_of(w);
    if (!h) return false;
    DWORD v = CONUI_DWMSBT_NONE;
    switch (m) {
    case Material::None:    v = CONUI_DWMSBT_NONE;    break;
    case Material::Blur:    v = CONUI_DWMSBT_ACRYLIC; break; // no separate blur on 11
    case Material::Acrylic: v = CONUI_DWMSBT_ACRYLIC; break;
    case Material::Mica:    v = CONUI_DWMSBT_MICA;    break;
    }
    const HRESULT hr = DwmSetWindowAttribute(h, DWMWA_SYSTEMBACKDROP_TYPE, &v, sizeof v);
    if (WinState* s = find(h)) { s->material = m; s->material_ok = SUCCEEDED(hr); }
    return SUCCEEDED(hr);
}

bool IsMaximized(GLFWwindow* w) {
    HWND h = hwnd_of(w);
    return h && IsZoomed(h);
}

bool IsFullscreen(GLFWwindow* w) {
    WinState* s = find(hwnd_of(w));
    return s && s->fullscreen;
}

void ToggleFullscreen(GLFWwindow* w) {
    HWND h = hwnd_of(w);
    WinState* s = find(h);
    if (!h || !s) return;

    if (!s->fullscreen) {
        s->saved_wp.length = sizeof s->saved_wp;
        if (!GetWindowPlacement(h, &s->saved_wp)) return;
        HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof mi;
        if (!GetMonitorInfoW(mon, &mi)) return;
        // A MAXIMISED window has to stop being one first. Windows keeps a
        // maximised window to the work area (the taskbar stays visible), and
        // the WM_NCCALCSIZE handler insets a zoomed window's client area on top of
        // that — so moving it onto the monitor rect removed the caption but
        // left the taskbar showing. Restoring through the placement, not
        // ShowWindow, skips the restore animation.
        if (IsZoomed(h)) {
            WINDOWPLACEMENT wp = s->saved_wp;
            wp.showCmd = SW_SHOWNORMAL;
            SetWindowPlacement(h, &wp);
        }
        // Set before the move: the move sends WM_NCCALCSIZE / WM_NCHITTEST,
        // and both already have to see the fullscreen state.
        s->fullscreen = true;
        // Borderless fullscreen: the whole monitor, styles untouched, so
        // alt-tab and the compositor keep behaving normally.
        SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        s->fullscreen = false;
        // Puts back the normal rectangle AND the maximised state it had.
        SetWindowPlacement(h, &s->saved_wp);
        SetWindowPos(h, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }
}

void PlaceWindow(GLFWwindow* w, int x, int y, int width, int height) {
    HWND h = hwnd_of(w);
    if (!h) return;
    if (!find(h)) {                       // no custom frame: GLFW's arithmetic holds
        glfwSetWindowSize(w, width, height);
        glfwSetWindowPos(w, x, y);
        return;
    }
    // With the custom frame the client rect IS the window rect (DWM draws the
    // border and shadow outside it), so the numbers go to Windows as they are.
    SetWindowPos(h, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ClampToWorkArea(GLFWwindow* w) {
    HWND h = hwnd_of(w);
    if (!h || IsZoomed(h) || IsIconic(h)) return;
    HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi)) return;

    RECT r{};
    GetWindowRect(h, &r);
    // Shrink first: a window saved on a big monitor, restored onto a small
    // one, would otherwise be pushed off the LEFT edge by the clamp below.
    const LONG workW = mi.rcWork.right - mi.rcWork.left;
    const LONG workH = mi.rcWork.bottom - mi.rcWork.top;
    LONG ww = r.right - r.left, wh = r.bottom - r.top;
    const bool shrink = ww > workW || wh > workH;
    if (ww > workW) ww = workW;
    if (wh > workH) wh = workH;
    LONG x = r.left, y = r.top;
    if (x + ww > mi.rcWork.right)  x = mi.rcWork.right - ww;
    if (y + wh > mi.rcWork.bottom) y = mi.rcWork.bottom - wh;
    if (x < mi.rcWork.left)   x = mi.rcWork.left;
    if (y < mi.rcWork.top)    y = mi.rcWork.top;
    if (x != r.left || y != r.top || shrink)
        SetWindowPos(h, nullptr, x, y, ww, wh,
                     (shrink ? 0u : (UINT)SWP_NOSIZE) | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Forget(GLFWwindow* w) {
    HWND h = hwnd_of(w);
    if (!h) return;
    if (WinState* s = find(h)) {
        SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)s->orig);
        g_windows.erase(h);
    }
}

std::string Describe(GLFWwindow* w) {
    HWND h = hwnd_of(w);
    if (!h) return "no native handle";
    const WinState* s = find(h);

    RECT wr{}, cr{};
    GetWindowRect(h, &wr);
    GetClientRect(h, &cr);
    POINT tl{ 0, 0 };
    ClientToScreen(h, &tl);

    const LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    DWORD corner = 0xFFFFFFFF;
    DwmGetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof corner);

    char buf[768];
    std::snprintf(buf, sizeof buf,
        "  subclassed      : %s\n"
        "  window rect     : %ldx%ld at (%ld,%ld)\n"
        "  client rect     : %ldx%ld, origin offset (%ld,%ld)\n"
        "  caption eaten   : %s (top inset %ld px)\n"
        "  styles kept     : THICKFRAME=%d CAPTION=%d MINBOX=%d MAXBOX=%d\n"
        "  corner pref     : %lu (1 = DONOTROUND)\n"
        "  resize margin   : %d x %d px\n"
        "  caption strip   : %d px, %d hole(s)\n"
        "  material        : %d (%s)\n",
        s && s->frame_installed ? "yes" : "no",
        wr.right - wr.left, wr.bottom - wr.top, wr.left, wr.top,
        cr.right - cr.left, cr.bottom - cr.top, tl.x - wr.left, tl.y - wr.top,
        (tl.y - wr.top) == 0 ? "yes" : "no", (long)(tl.y - wr.top),
        (st & WS_THICKFRAME) ? 1 : 0, (st & WS_CAPTION) == WS_CAPTION ? 1 : 0,
        (st & WS_MINIMIZEBOX) ? 1 : 0, (st & WS_MAXIMIZEBOX) ? 1 : 0,
        (unsigned long)corner, frame_x(), frame_y(),
        s ? s->caption_px : 0, s ? (int)s->holes.size() : 0,
        s ? (int)s->material : -1, s && s->material_ok ? "accepted" : "refused");
    return buf;
}

} // namespace platform

#endif // _WIN32
