#include "Platform.hpp"

// ---------------------------------------------------------------------------
// macOS path. UNVERIFIED — written from the documented API, never run on
// hardware, like `build_macos.sh`.
// Anything here that turns out wrong should be fixed on a real machine, not
// guessed at further.
//
// The shape of the deal is the same as on Windows: NSWindow keeps the frame,
// the shadow and the resize edges; the titlebar is made transparent and the
// three standard buttons are hidden, so the content view covers the whole
// window and ConUI draws the caption itself.
//
// The one difference that cannot be removed: NSWindow always rounds its corners and
// offers no way to square them (DESIGN_SYSTEM.md §8.1).
// ---------------------------------------------------------------------------

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <cstdio>
#include <unordered_map>
#include <vector>

namespace {

struct MacState {
    int captionPx = 20;
    std::vector<std::pair<int,int>> holes;
    bool frame_installed = false;
    platform::Material material = platform::Material::None;
    NSVisualEffectView* effect = nil;
};
std::unordered_map<GLFWwindow*, MacState> g_windows;

NSWindow* ns_of(GLFWwindow* w) { return w ? (NSWindow*)glfwGetCocoaWindow(w) : nil; }

} // namespace

namespace platform {

bool InstallCustomFrame(GLFWwindow* w, int captionPx) {
    NSWindow* win = ns_of(w);
    if (!win) return false;

    win.titlebarAppearsTransparent = YES;
    win.titleVisibility = NSWindowTitleVisibilityHidden;
    win.styleMask |= NSWindowStyleMaskFullSizeContentView;
    [[win standardWindowButton:NSWindowCloseButton]       setHidden:YES];
    [[win standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
    [[win standardWindowButton:NSWindowZoomButton]        setHidden:YES];

    // The caption row drags the window. AppKit gives this for free once the
    // titlebar is transparent, as long as the content does not eat the event.
    win.movableByWindowBackground = YES;

    MacState s;
    s.captionPx = captionPx > 0 ? captionPx : 20;
    s.frame_installed = true;
    g_windows[w] = s;
    return true;
}

void SetCaptionHeight(GLFWwindow* w, int captionPx) {
    auto it = g_windows.find(w);
    if (it != g_windows.end()) it->second.captionPx = captionPx > 0 ? captionPx : 20;
}

void SetCaptionHoles(GLFWwindow* w, const int* x0, const int* x1, int count) {
    auto it = g_windows.find(w);
    if (it == g_windows.end()) return;
    it->second.holes.clear();
    for (int i = 0; i < count; ++i) it->second.holes.emplace_back(x0[i], x1[i]);
    // AppKit has no WM_NCHITTEST equivalent; movableByWindowBackground is
    // suppressed per-event by the app instead (see AppWindow input handling).
}

bool SetMaterial(GLFWwindow* w, Material m) {
    NSWindow* win = ns_of(w);
    if (!win) return false;
    auto it = g_windows.find(w);
    if (it == g_windows.end()) return false;

    if (m == Material::None) {
        if (it->second.effect) { [it->second.effect removeFromSuperview]; it->second.effect = nil; }
        it->second.material = m;
        return true;
    }
    if (!it->second.effect) {
        NSView* content = win.contentView;
        NSVisualEffectView* fx = [[NSVisualEffectView alloc] initWithFrame:content.bounds];
        fx.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        fx.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        fx.state = NSVisualEffectStateActive;
        [content addSubview:fx positioned:NSWindowBelow relativeTo:nil];
        it->second.effect = fx;
    }
    // Discrete materials only — there is no blur radius to set, which is the
    // same limitation the Windows side has (§6).
    switch (m) {
    case Material::Blur:    it->second.effect.material = NSVisualEffectMaterialUnderWindowBackground; break;
    case Material::Acrylic: it->second.effect.material = NSVisualEffectMaterialHUDWindow;             break;
    case Material::Mica:    it->second.effect.material = NSVisualEffectMaterialWindowBackground;      break;
    default: break;
    }
    it->second.material = m;
    return true;
}

bool IsMaximized(GLFWwindow* w) {
    NSWindow* win = ns_of(w);
    return win && win.isZoomed;
}

bool IsFullscreen(GLFWwindow* w) {
    NSWindow* win = ns_of(w);
    return win && (win.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

void ToggleFullscreen(GLFWwindow* w) {
    NSWindow* win = ns_of(w);
    if (win) [win toggleFullScreen:nil];
}

void ClampToWorkArea(GLFWwindow* w) {
    // AppKit already keeps windows inside the visible frame; the DWM bug this
    // works around on Windows has no counterpart here.
    (void)w;
}

void PlaceWindow(GLFWwindow* w, int x, int y, int width, int height) {
    // UNVERIFIED: with FullSizeContentView GLFW's content rect should already
    // be the whole window, so its own setters are the right ones here.
    glfwSetWindowSize(w, width, height);
    glfwSetWindowPos(w, x, y);
}

void Forget(GLFWwindow* w) { g_windows.erase(w); }

std::string Describe(GLFWwindow* w) {
    NSWindow* win = ns_of(w);
    if (!win) return "no native handle";
    auto it = g_windows.find(w);
    char buf[512];
    std::snprintf(buf, sizeof buf,
        "  custom frame    : %s (UNVERIFIED — never run on hardware)\n"
        "  titlebar        : transparent, title hidden, 3 buttons hidden\n"
        "  corners         : rounded, NSWindow gives no way to square them\n"
        "  caption strip   : %d px\n",
        (it != g_windows.end() && it->second.frame_installed) ? "installed" : "no",
        it != g_windows.end() ? it->second.captionPx : 0);
    return buf;
}

} // namespace platform

#endif // __APPLE__
