#pragma once
#include <string>

struct GLFWwindow;

// ---------------------------------------------------------------------------
// Everything the operating system does for the app, behind one interface
// (docs/DESIGN_SYSTEM.md §8.1).
//
// The split is deliberate and sharp: the OS owns the frame — border, shadow,
// square corners, resize edges, snap, minimise/maximise animations — and the app
// owns the caption drawn inside the client area. Nothing here draws anything;
// it only arranges for the window to behave natively while looking like ConUI.
//
// The Windows path is the load-bearing one and is exercised by --probe. The
// macOS path is written but has never run on hardware;
// every function there is marked UNVERIFIED.
// ---------------------------------------------------------------------------

namespace platform {

/** OS backdrop material behind a translucent window (§6). */
enum class Material { None = 0, Blur = 1, Acrylic = 2, Mica = 3 };

/**
 * Turns a normal GLFW window into "native behaviour, ConUI caption":
 * the caption strip is folded into the client rect, the border stays, and the
 * corners are squared. Safe to call once per window, after creation.
 *
 * @param captionPx height of the drag strip, in pixels — one cell row.
 * @return false when the platform has no custom-frame path (then the window
 *         keeps its ordinary system caption and everything still works).
 */
bool InstallCustomFrame(GLFWwindow* w, int captionPx);

/** Updates the drag strip height after a density or DPI change. */
void SetCaptionHeight(GLFWwindow* w, int captionPx);

/**
 * Marks a horizontal span of the caption row as interactive, so clicks there
 * reach the app instead of being swallowed as "drag the window". Spans are in
 * pixels from the window's left edge; pass count = 0 to clear.
 */
void SetCaptionHoles(GLFWwindow* w, const int* x0, const int* x1, int count);

/** Applies the compositor material. Returns false if the OS refused it. */
bool SetMaterial(GLFWwindow* w, Material m);

/** True while the window is maximised (not the same as fullscreen). */
bool IsMaximized(GLFWwindow* w);

/** Borderless fullscreen on the window's current monitor, and back. */
void ToggleFullscreen(GLFWwindow* w);
bool IsFullscreen(GLFWwindow* w);

/**
 * Pulls a window back inside the monitor work area. Works around a DWM quirk
 * on restore-from-minimised.
 */
void ClampToWorkArea(GLFWwindow* w);

/**
 * Puts the window's CONTENT area at (x, y) with size w × h, in screen
 * coordinates — the same numbers glfwGetWindowPos/Size report. Use this, not
 * glfwSetWindowPos/Size: GLFW converts through the window styles and adds a
 * frame and caption that the custom frame folded into the client area, so
 * every restore would grow the window by one caption and shift it up.
 */
void PlaceWindow(GLFWwindow* w, int x, int y, int width, int height);

/** Human-readable state of the native frame, for --probe. */
std::string Describe(GLFWwindow* w);

/** Drops per-window bookkeeping. Call before glfwDestroyWindow(). */
void Forget(GLFWwindow* w);

} // namespace platform
