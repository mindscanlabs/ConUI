#include "Platform.hpp"

// Everything that is not Windows, and not the Cocoa path in Platform_mac.mm:
// Linux and the BSDs, through GLFW alone. The window keeps the desktop's own
// title bar (there is no portable way to fold it into the client area), so
// the app runs with an ordinary system caption above ConUI's. Fullscreen and
// the work-area clamp use plain GLFW calls. UNVERIFIED: not yet run on Linux.
#if !defined(_WIN32) && !defined(__APPLE__)

#include <GLFW/glfw3.h>

#include <algorithm>
#include <map>

namespace platform {

namespace {

/** The windowed geometry to return to when fullscreen ends. */
struct Placement { int x = 0, y = 0, w = 0, h = 0; };
std::map<GLFWwindow*, Placement> g_windowed;

/** The monitor the window overlaps most; the primary one if none. */
GLFWmonitor* MonitorOf(GLFWwindow* w) {
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(w, &wx, &wy);
    glfwGetWindowSize(w, &ww, &wh);
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* best = glfwGetPrimaryMonitor();
    long bestArea = 0;
    for (int i = 0; i < count; ++i) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode) continue;
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const int ox = std::max(0, std::min(wx + ww, mx + mode->width) - std::max(wx, mx));
        const int oy = std::max(0, std::min(wy + wh, my + mode->height) - std::max(wy, my));
        const long area = (long)ox * oy;
        if (area > bestArea) { bestArea = area; best = monitors[i]; }
    }
    return best;
}

} // namespace

bool InstallCustomFrame(GLFWwindow*, int) { return false; }
void SetCaptionHeight(GLFWwindow*, int) {}
void SetCaptionHoles(GLFWwindow*, const int*, const int*, int) {}
bool SetMaterial(GLFWwindow*, Material) { return false; }

bool IsMaximized(GLFWwindow* w) {
    return w && glfwGetWindowAttrib(w, GLFW_MAXIMIZED) == GLFW_TRUE;
}

bool IsFullscreen(GLFWwindow* w) { return w && glfwGetWindowMonitor(w) != nullptr; }

void ToggleFullscreen(GLFWwindow* w) {
    if (!w) return;
    if (IsFullscreen(w)) {
        const auto it = g_windowed.find(w);
        Placement p = it != g_windowed.end() ? it->second : Placement{ 100, 100, 1024, 640 };
        if (p.w <= 0 || p.h <= 0) p = Placement{ 100, 100, 1024, 640 };
        glfwSetWindowMonitor(w, nullptr, p.x, p.y, p.w, p.h, GLFW_DONT_CARE);
        return;
    }
    Placement p;
    glfwGetWindowPos(w, &p.x, &p.y);
    glfwGetWindowSize(w, &p.w, &p.h);
    g_windowed[w] = p;
    GLFWmonitor* m = MonitorOf(w);
    const GLFWvidmode* mode = m ? glfwGetVideoMode(m) : nullptr;
    if (!mode) return;
    glfwSetWindowMonitor(w, m, 0, 0, mode->width, mode->height, mode->refreshRate);
}

void ClampToWorkArea(GLFWwindow* w) {
    if (!w || IsFullscreen(w)) return;
    GLFWmonitor* m = MonitorOf(w);
    if (!m) return;
    int ax = 0, ay = 0, aw = 0, ah = 0;
    glfwGetMonitorWorkarea(m, &ax, &ay, &aw, &ah);
    if (aw <= 0 || ah <= 0) return;
    int x = 0, y = 0, ww = 0, wh = 0;
    glfwGetWindowPos(w, &x, &y);
    glfwGetWindowSize(w, &ww, &wh);
    const int nx = std::max(ax, std::min(x, ax + aw - ww));
    const int ny = std::max(ay, std::min(y, ay + ah - wh));
    if (nx != x || ny != y) glfwSetWindowPos(w, nx, ny);
}

void PlaceWindow(GLFWwindow* w, int x, int y, int width, int height) {
    glfwSetWindowSize(w, width, height);   // no custom frame here: GLFW is right
    glfwSetWindowPos(w, x, y);
}

void Forget(GLFWwindow* w) { g_windowed.erase(w); }

std::string Describe(GLFWwindow*) {
    return "  custom frame    : not implemented on this platform (system title bar)\n";
}

} // namespace platform

#endif
