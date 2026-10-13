#pragma once
#include <cstddef>
#include <string>

class ThemeStore;
class RasterStore;
struct AppState;
namespace ui { class Context; class Layout; }

// ---------------------------------------------------------------------------
// The seam between the ConUI core and an application.
//
// main.cpp owns the windows, the loop, the renderer, the theme store and
// data.bin; it knows nothing about what is drawn. An application is these few
// functions. The playground implements them in App/Playground.cpp, the empty
// fork template in App/Skeleton.cpp, and CMake builds exactly one of the two
// (CONUI_APP=playground|skeleton).
//
// A fork starts by copying Skeleton.cpp — see docs/FORK.md.
// ---------------------------------------------------------------------------

/** What the core lends the application for one frame. */
struct AppServices {
    ThemeStore&  themes;
    AppState&    state;        // persisted to data.bin on exit; edit it freely
    ui::Layout&  layout;       // split fractions, persisted too
    RasterStore& rasters;
    int          themeIndex;   // the theme both windows are drawn with
    double       fps;          // measured by the loop, smoothed
    size_t       lastVerts;    // the size of the last frame's one draw call
    bool         fullscreen;   // THIS window, right now — no caption is drawn,
                               // so the app can reclaim its rows
};

/**
 * What only the loop can do. The application asks; the loop acts after the
 * frame, so a frame never changes the world it is still drawing.
 */
struct AppActions {
    bool quit = false;
    int  theme = -1;            // index into ThemeStore
    int  density = -1;          // a Density value: rebuilds the atlas and the grids
    bool toggle_fullscreen = false;
    bool open_second = false;   // (re)open the second window
    bool close_second = false;
};

namespace app {

/** The main window's caption and its size in cells when first opened. */
const char* Title();
void MainSize(int& cols, int& rows);
/** The second window's caption; nullptr for a single-window application. */
const char* SecondTitle();
void SecondSize(int& cols, int& rows);

/** Once, after the GL context and the stores exist. */
void Init(AppServices& s);
/** One frame of each window's body — everything below the caption. */
void MainFrame(ui::Context& ctx, AppServices& s, AppActions& a);
void SecondFrame(ui::Context& ctx, AppServices& s, AppActions& a);

/**
 * The application's own state, kept in data.bin (AppState::app_data). Load is
 * called once after Init with what the last run saved (possibly empty, or
 * from an older version — tolerate both); Save is called by the autosave
 * every few seconds and on exit. The format is the application's business;
 * the playground uses JSON.
 */
void LoadState(const std::string& data);
std::string SaveState();

} // namespace app
