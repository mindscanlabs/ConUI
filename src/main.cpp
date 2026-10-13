#include "App/App.hpp"
#include "App/Scene.hpp"      // BuildSelfTestScene, for --dump
#include "App/SelfTest.hpp"
#include "Core/AppWindow.hpp"
#include "Core/IconSheets.hpp"
#include "Core/Platform.hpp"
#include "Core/RasterStore.hpp"
#include "Core/ResourceManager.hpp"
#include "Core/ThemeStore.hpp"
#include "Core/Utils.hpp"
#include "Render/CellBuffer.hpp"
#include "Render/FontAtlas.hpp"
#include "Render/GLLoader.hpp"
#include "Render/Renderer.hpp"
#include "UI/Theme.hpp"
#include "UI/WindowChrome.hpp"
#include "UI/Context.hpp"
#include "UI/Layout.hpp"
#include "UI/Strings.hpp"

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GLFW/glfw3native.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> LoadFont() {
    size_t n = 0;
    const void* p = ResourceManager::Get().GetResource("JetBrainsMono-Regular.ttf", n);
    if (p && n) return std::vector<uint8_t>((const uint8_t*)p, (const uint8_t*)p + n);
    return {};
}

platform::Material MaterialOf(Backdrop b) {
    switch (b) {
    case Backdrop::None:    return platform::Material::None;
    case Backdrop::Blur:    return platform::Material::Blur;
    case Backdrop::Acrylic: return platform::Material::Acrylic;
    case Backdrop::Mica:    return platform::Material::Mica;
    }
    return platform::Material::None;
}

#ifdef _WIN32
void SetWindowIcon(GLFWwindow* w) {
    HWND hwnd = (HWND)glfwGetWin32Window(w);
    HICON icon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(102));
    if (!icon) return;
    SendMessage(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)icon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
}
#else
void SetWindowIcon(GLFWwindow*) {}
#endif

#ifdef _WIN32
/**
 * The app is a GUI-subsystem exe (no console window on a double-click), and a
 * GUI exe gets no console of its own. Run from a terminal with --selftest,
 * --probe or --dump it borrows the parent's, so the output still shows.
 * Output that is already redirected (a pipe, a file — build.sh, mintty) is
 * left exactly where it goes.
 */
void AttachParentConsole() {
    const DWORD t = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    if (t == FILE_TYPE_PIPE || t == FILE_TYPE_DISK) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;       // double-clicked: no parent console
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
}
#endif

void OnScroll(GLFWwindow* w, double /*dx*/, double dy) {
    if (auto* self = (AppWindow*)glfwGetWindowUserPointer(w)) self->pushScroll((float)dy);
}

void OnChar(GLFWwindow* w, unsigned int cp) {
    if (auto* self = (AppWindow*)glfwGetWindowUserPointer(w)) self->pushChar(cp);
}

// One window's whole frame — pump, build, draw, swap. Set once main() has
// every object it closes over; the main loop calls it once a tick, and it is
// ALSO the window's refresh callback, because on Windows an interactive
// drag-resize runs its own nested message loop that blocks the main loop's
// glfwWaitEventsTimeout() for as long as the drag lasts. Without a callback
// that renders from inside that nested loop, the window only repaints once
// the mouse button comes back up — it doesn't visibly track the drag.
std::function<void(AppWindow*)> g_renderOne;

void OnWindowRefresh(GLFWwindow* w) {
    if (auto* self = (AppWindow*)glfwGetWindowUserPointer(w))
        if (g_renderOne) g_renderOne(self);
}

/** Routes a window's typed text, wheel and repaint requests to its AppWindow. */
void AttachCallbacks(AppWindow& w) {
    glfwSetWindowUserPointer(w.handle(), &w);
    glfwSetCharCallback(w.handle(), OnChar);
    glfwSetScrollCallback(w.handle(), OnScroll);
    glfwSetWindowRefreshCallback(w.handle(), OnWindowRefresh);
}

/** Startup failed after glfwInit(): say why and leave. */
int Fail(const char* why) {
    std::fprintf(stderr, "%s\n", why);
    glfwTerminate();
    return 1;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    AttachParentConsole();
#endif
    bool selftest = false, dump = false, probe = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;
        else if (std::strcmp(argv[i], "--dump") == 0) dump = true;
        else if (std::strcmp(argv[i], "--probe") == 0) probe = true;
    }

    // --dump needs no GL at all: the cell grid is the source of truth and it
    // is plain data (DESIGN_SYSTEM §2).
    if (dump) {
        CellBuffer cb;
        BuildSelfTestScene(cb);
        std::string s = cb.dump();
        std::fwrite(s.data(), 1, s.size(), stdout);
        return 0;
    }

    AppState state;
    LoadAppState(state);
    // The material is always None: an older data.bin may still
    // hold Blur/Acrylic/Mica from when it could be picked, and nothing in the
    // UI could turn that back off. The field stays in data.bin for its layout.
    state.backdrop = Backdrop::None;

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    float xscale = 1.0f, yscale = 1.0f;
    if (monitor) glfwGetMonitorContentScale(monitor, &xscale, &yscale);
#ifdef __APPLE__
    global_scale = 1.0f; // macOS reports HiDPI via framebuffer size instead
#else
    global_scale = xscale;
#endif
    AdjustAppStateToScale(state, global_scale);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    if (selftest) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    const float fontPx = FontPxForDensity(state.density) * global_scale;

    if (selftest) {
        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
        GLFWwindow* hidden = glfwCreateWindow(64, 64, "ConUI", nullptr, nullptr);
        if (!hidden) return Fail("glfwCreateWindow failed");
        glfwMakeContextCurrent(hidden);
        if (!gl::Load()) return Fail("OpenGL 3.3 entry points missing");
        const int rc = RunSelfTest(GetExeDirectory().c_str());
        glfwDestroyWindow(hidden);
        glfwTerminate();
        return rc;
    }

    // ── the main window ─────────────────────────────────────────────────────
    int mainCols = 96, mainRows = 34;
    app::MainSize(mainCols, mainRows);
    AppWindow main_win;
    if (!main_win.create(app::Title(), mainCols, mainRows, fontPx, nullptr))
        return Fail("glfwCreateWindow failed");
    glfwMakeContextCurrent(main_win.handle());
    glfwSwapInterval(1);
    if (!gl::Load()) return Fail("OpenGL 3.3 entry points missing");
    SetWindowIcon(main_win.handle());
    // Where the user left it last time (a first run keeps the default).
    main_win.applyGeometry(state.main_window);
    platform::SetMaterial(main_win.handle(), MaterialOf(state.backdrop));
    AttachCallbacks(main_win);

    // ── an optional second REAL OS window, sharing the context and the atlas
    // (the playground's theme editor, §8.16). It can be closed and opened
    // again, so creating it is a function rather than a one-off.
    AppWindow second_win;
    auto openSecond = [&]() -> bool {
        if (second_win.handle()) return true;
        if (!app::SecondTitle()) return false;          // a single-window app
        int cols = 60, rows = 34;
        app::SecondSize(cols, rows);
        if (!second_win.create(app::SecondTitle(), cols, rows, main_win.fontPx(),
                               main_win.handle()))
            return false;
        platform::SetMaterial(second_win.handle(), MaterialOf(state.backdrop));
        int mx = 0, my = 0;
        glfwGetWindowPos(main_win.handle(), &mx, &my);
        if (state.theme_editor.w > 0) {
            second_win.applyGeometry(state.theme_editor);
        } else {
            int sw = 0, sh = 0;
            glfwGetWindowSize(second_win.handle(), &sw, &sh);
            platform::PlaceWindow(second_win.handle(), mx + main_win.cols() * main_win.cellW() / 2,
                                  my + 4 * main_win.cellH(), sw, sh);
        }
        AttachCallbacks(second_win);
        return true;
    };
    const bool has_second = (state.theme_editor_open || probe) && openSecond();

    if (probe) {
        std::printf("ConUI native-frame probe\n\n[main window]\n%s",
                    platform::Describe(main_win.handle()).c_str());
        if (has_second)
            std::printf("\n[second window]\n%s",
                        platform::Describe(second_win.handle()).c_str());
        chrome::ButtonSpan spans[chrome::BTN_COUNT];
        chrome::ButtonSpans(main_win.cols(), main_win.cellW(), main_win.cellH(), spans);
        std::printf("\n[caption boxes, px from left]\n");
        static const char* kNames[] = { "close", "collapse", "zoom", "fullscreen" };
        for (int b = 0; b < chrome::BTN_COUNT; ++b)
            std::printf("  %-10s : %4d .. %4d\n", kNames[b], spans[b].x0, spans[b].x1);
        std::printf("\n[grid]\n  %dx%d cells of %dx%d px, spill %dx%d px\n",
                    main_win.cols(), main_win.rows(),
                    main_win.cellW(), main_win.cellH(),
                    main_win.spillX(), main_win.spillY());
        second_win.destroy();
        main_win.destroy();
        glfwTerminate();
        return 0;
    }

    // Read-only resources and writable data may live in different places
    // (a macOS bundle cannot be written to); on Windows they are one folder.
    ResourceManager::Get().Load(GetResourceDirectory() + "resources.dat");
    std::vector<uint8_t> ttf = LoadFont();
    if (ttf.empty()) return Fail("font missing from resources.dat");

    FontAtlas atlas;
    if (!atlas.init(ttf, main_win.cellW(), main_win.cellH())) return Fail("font atlas init failed");

    Renderer renderer;
    if (!renderer.init()) return Fail("renderer init failed");

    RasterStore rasters;
    rasters.init();

    // Themes come from disk, with the built-ins as the floor (§15).
    ThemeStore themes;
    themes.init(GetDataDirectory());
    for (const std::string& w : themes.warnings())
        std::fprintf(stderr, "theme: %s\n", w.c_str());
    int themeIndex = themes.indexOf(state.theme.c_str());
    if (themeIndex < 0) themeIndex = 0;

    str::SetLocale(str::LocaleFromCode(state.locale.c_str()));

    // Split fractions come back from data.bin; the nodes learn their defaults
    // the first time the application asks for them (§8.17).
    ui::Layout layout;
    layout.deserialize(state.layout);

    double fps = 60.0, lastTime = glfwGetTime();
    {
        AppServices s{ themes, state, layout, rasters, themeIndex, fps, 0, false };
        app::Init(s);
        app::LoadState(state.app_data);
    }

    AppWindow* windows[2] = { &main_win, &second_win };
    ui::Context contexts[2];

    // The caption box icons are raster masks packed into resources.dat, one
    // strip per pixel size (§8.1), loaded through the same LoadIconStrip as
    // any other icon set. If there is none at all, the caption draws its
    // boxes with geometry.
    std::map<int, chrome::CaptionIcons> iconCache;
    auto captionIcons = [&](int cellH) -> const chrome::CaptionIcons* {
        const int want = chrome::CaptionBoxPx(cellH);
        auto it = iconCache.find(want);
        if (it == iconCache.end()) {
            chrome::CaptionIcons ic;
            if (const IconStrip* s = LoadIconStrip(rasters, "caption", want, chrome::BTN_COUNT + 1)) {
                for (int i = 0; i <= chrome::BTN_COUNT; ++i) ic.id[i] = s->ids[(size_t)i];
                ic.px = s->px;
            }
            it = iconCache.emplace(want, ic).first;
        }
        return it->second.valid() ? &it->second : nullptr;
    };

    // One window's whole frame, closed over everything above. Assigned to
    // the file-scope std::function so it can double as the GLFW refresh
    // callback (see its declaration) as well as what the main loop calls.
    g_renderOne = [&](AppWindow* w) {
        if (!w->handle()) return;
        if (w->shouldClose() && w != &main_win) {
            // Closing a secondary window must not take the app with it.
            // Its place is remembered first: reopened, it comes back there.
            w->saveGeometry(state.theme_editor);
            w->destroy();
            state.theme_editor_open = 0;
            return;
        }

        glfwMakeContextCurrent(w->handle());
        w->pumpCaptionInput();
        w->pumpUiInput();
        // An edge, not a level: a key press lasts several frames, and a
        // level check toggled fullscreen once per frame for as long as F11
        // was held — the final state was a coin toss.
        if (w->uiInput().key_f11) platform::ToggleFullscreen(w->handle());
        w->beginFrame();
        w->drawCaption(captionIcons(w->cellH()));

        const bool isMain = w == &main_win;
        ui::Context& ctx = contexts[isMain ? 0 : 1];
        AppServices s{ themes, state, layout, rasters, themeIndex, fps,
                       renderer.lastVertexCount(), platform::IsFullscreen(w->handle()) };
        AppActions a;
        ctx.begin(w->cells(), w->uiInput());
        ctx.setGlitches(state.glitches != 0);   // the user's switch, every kind (§9)
        if (isMain) app::MainFrame(ctx, s, a);
        else                app::SecondFrame(ctx, s, a);
        ctx.end();
        // The caption boxes are buttons too: the pointer, like every other
        // clickable thing (§9). The caption is not the UI context's, so it is
        // decided here.
        w->applyCursor(w->hoveredButton() != chrome::BTN_NONE ? ui::Cursor::Pointer : ctx.cursor());
        w->applyClipboard(ctx);

        // Acting on the frame's requests outside the frame keeps drawing
        // free of side effects: the screen it drew is the screen it
        // described, and the change lands on the next one.
        //
        // Esc and Ctrl+Q quit from the main window — unless a text field
        // or an open popup kept the key. In a second window Esc closes it.
        const ui::Input& in = w->uiInput();
        const bool escape = in.key_escape && !ctx.escapeClaimed();
        if (a.quit || (isMain && (in.key_quit || escape))) main_win.requestClose();
        if (!isMain && escape) a.close_second = true;
        if (a.theme >= 0 && a.theme < themes.count()) themeIndex = a.theme;
        if (a.toggle_fullscreen) platform::ToggleFullscreen(w->handle());
        if (a.open_second && openSecond()) state.theme_editor_open = 1;
        if (a.close_second) second_win.requestClose();
        if (a.density >= 0 && a.density <= (int)Density::Tiny &&
            a.density != (int)state.density) {
            // Density is the cell size (§3): a new atlas at the new cell,
            // and both grids rebuilt from their windows' pixel sizes. The
            // windows keep their size on screen; they hold more or fewer
            // cells.
            state.density = (Density)a.density;
            const float px = FontPxForDensity(state.density) * global_scale;
            for (AppWindow* x : windows) if (x->handle()) x->setFontPx(px);
            atlas.init(ttf, main_win.cellW(), main_win.cellH());
        }

        // Taken HERE, not at the top of the loop: the theme editor's NEW
        // may have grown the store this frame, and a reference from before
        // would dangle.
        const Theme& theme = themes.at(themeIndex);
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(w->handle(), &fbw, &fbh);
        const Rgba& bg = theme.c[BG_WINDOW];
        glViewport(0, 0, fbw, fbh);
        // Cleared fully transparent, same RGB as the window background so
        // blending at any alpha never tints it: the actual alpha is the
        // renderer's own window-background pass, which now covers the
        // WHOLE framebuffer (not just the cell grid) at `state.opacity`,
        // so the compositor sees one uniform alpha across the window (§6).
        glClearColor(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // In fullscreen there is no caption: row 0 is plain content (no
        // title nudge), and the grid is shifted to leave half a cell free
        // under the status bar (AppWindow::beginFrame).
        const bool fs = platform::IsFullscreen(w->handle());
        renderer.draw(w->cells(), atlas, theme, fbw, fbh, 0, w->gridOffsetY(), &rasters,
                      w->handle(), state.opacity,
                      fs ? 0.0f : chrome::CaptionTextPadPx(w->cellH()), state.panel_opacity);
        glfwSwapBuffers(w->handle());
    };

    // ── data.bin: what is saved, when ─────────────────────────
    // Everything worth keeping is gathered into `state` in one place, so the
    // autosave and the exit save cannot drift apart.
    auto captureState = [&]() {
        state.theme    = themes.at(themeIndex).name;
        state.locale   = str::LocaleCode(str::GetLocale());
        state.layout   = layout.serialize();
        state.app_data = app::SaveState();
        state.saved_gs = global_scale;
        main_win.saveGeometry(state.main_window);
        if (second_win.handle()) second_win.saveGeometry(state.theme_editor);
    };
    // Every 5 s, but only when something changed: an idle app
    // does not rewrite its settings file twelve times a minute.
    std::vector<char> lastSaved = SerializeAppState(state);
    double nextAutosave = glfwGetTime() + 5.0;
    auto autosave = [&]() {
        captureState();
        std::vector<char> now = SerializeAppState(state);
        if (now != lastSaved && SaveAppState(state)) lastSaved.swap(now);
    };

    // ── the loop walks every window: events, frame, swap ────────────────────
    //
    // It draws on demand, not sixty times a second: the wait ends on the next
    // input event, or when the soonest thing that moves by itself is due —
    // each frame says so with Context::animate() (§9). A slow heartbeat backs
    // that up, so an animation that forgot to ask still runs, just coarsely,
    // and so the autosave and the clock keep ticking with nothing happening.
    constexpr double kHeartbeat = 0.25;
    while (!main_win.shouldClose()) {
        if (glfwGetTime() >= nextAutosave) {
            autosave();
            nextAutosave = glfwGetTime() + 5.0;
        }
        double wait = kHeartbeat;
        for (const ui::Context& c : contexts) {
            const double due = c.nextFrameIn();
            if (due >= 0.0 && due < wait) wait = due;
        }
        glfwWaitEventsTimeout(std::max(0.0, wait));

        // The frame rate, smoothed — the status bar shows a number, not noise.
        const double now = glfwGetTime();
        const double dt = now - lastTime;
        lastTime = now;
        if (dt > 0.0) fps = fps * 0.9 + (1.0 / dt) * 0.1;

        for (AppWindow* w : windows) g_renderOne(w);
    }

    // Captured while the windows still exist — their geometry goes with them.
    captureState();
    SaveAppState(state);

    renderer.shutdown();
    second_win.destroy();
    main_win.destroy();
    glfwTerminate();
    return 0;
}
