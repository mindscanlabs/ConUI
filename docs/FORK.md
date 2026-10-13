# Building an application on ConUI

ConUI builds two applications from one core. They differ only in what is
compiled from `src/App/`:

| Target | What it is | Build |
|---|---|---|
| **playground** | the showcase: every widget, every state, the theme editor | `./build.sh` → `dist/win/` |
| **skeleton** | an empty application, the starting point for a new one | `CONUI_APP=skeleton ./build.sh` → `dist/win-skeleton/` |

The skeleton already has everything that is not specific to an application,
and nothing more:

- a native OS window with the System 7 caption (Design System §8.1): Snap,
  the OS shadow, resize edges, fullscreen on `F11`;
- themes from `themes/*.json`, with the built-in themes as a fallback (§15);
- the localisation layer: English and Russian, `T(Id)` (§14);
- `data.bin`: theme, locale, window and section opacity, density, window
  geometry, split fractions, and the application's own state;
- every widget in `src/UI/Widgets/` and the split tree in `src/UI/Layout.*`;
- one draw call per frame, frames drawn on demand;
- `--selftest` with the core checks: rendering, font, JSON, themes, strings,
  widget behaviour.

The section numbers below (`§8.1`, `§14`) refer to
[DESIGN_SYSTEM.md](DESIGN_SYSTEM.md).

## 1. Building the skeleton

```bash
CONUI_APP=skeleton ./build.sh run
```

On macOS and Linux: `CONUI_APP=skeleton ./build_macos.sh` or
`CONUI_APP=skeleton ./build_linux.sh run`. Or by hand:
`cmake -DCONUI_APP=skeleton ..`. The `CONUI_APP` switch in `CMakeLists.txt`
decides which files from `src/App/` go into the build; the core is always the
same.

## 2. Where to start

**`src/App/Skeleton.cpp`, the function `app::MainFrame()`.** It is the body of
the window: everything below the caption. Copy the file under a new name (for
example `src/App/MyApp.cpp`), add a branch for it to the `CONUI_APP` choice in
`CMakeLists.txt`, and work there. The playground then stays available as a
reference.

```cmake
elseif(CONUI_APP STREQUAL "myapp")
    set(APP_SOURCES src/App/MyApp.cpp)
    set(APP_DEFINES CONUI_MYAPP=1)
```

Build it with `CONUI_APP=myapp ./build.sh` (or `build_macos.sh`,
`build_linux.sh`). The scripts give the skeleton its own build folder and put
every other application into the default one (`build/` and `dist/win/` on
Windows); give a new application its own folders there too if the playground
should keep building side by side.

The whole contract between the core and an application is
`src/App/App.hpp`:

| Function | What it does |
|---|---|
| `Title()`, `MainSize()` | the window caption, and the size in cells on first start |
| `LoadState()`, `SaveState()` | the application's own state in `data.bin` (section 5) |
| `SecondTitle()`, `SecondSize()` | a second OS window; `nullptr` means a single window |
| `Init()` | once: load pictures, open files |
| `MainFrame()`, `SecondFrame()` | one frame of a window body |

`AppServices` is what the core lends for one frame: the themes, `AppState`, the
layout, the rasters, the FPS and the `fullscreen` flag of that window.
`AppActions` is what the application asks the loop to do: quit, change the
theme or density, toggle fullscreen, open or close the second window. The loop
carries out these requests **after** the frame, so a frame never changes the
world it is still drawing.

## 3. Writing a frame

Immediate mode, as in Dear ImGui: each frame describes the whole screen
again, and user actions come back as the return values of widget calls.

```cpp
void MainFrame(ui::Context& ctx, AppServices& s, AppActions& a) {
    const int W = ctx.cells().width(), H = ctx.cells().height();
    // No caption in fullscreen, so its rows go to the content.
    const int top = s.fullscreen ? 0 : chrome::CONTENT_TOP;
    // H-2 is the rule, H-1 the status bar; the content ends at H-3.
    const ui::Rect body{ 1, top, W - 2, H - 2 - top };
    ui::Rect left, right;
    ui::Split(ctx, s.layout, "main", ui::Axis::Cols, body, 0.4f, 20, 30, left, right);

    // ID_MY_FILES is a new string in src/UI/Strings.* (rule 1 below)
    ui::BeginPanel(ctx, "files", str::T(str::ID_MY_FILES), left);
    if (ui::Button(ctx, str::ID_OPEN)) openFile();
    ui::EndPanel(ctx);

    if (quitRequested) a.quit = true;
}
```

The rules the system rests on. An application should keep them too:

1. **Every label goes through `T()`.** A string is added to `enum Id` in
   `src/UI/Strings.hpp` and to the table in `Strings.cpp` at the same position,
   with a width limit in cells and one column per locale. The self-test fails
   the build if a translation does not fit.
2. **A region's id is separate from its title.** `BeginPanel(ctx, "files",
   T(...))`: the first argument is constant, the second is localised.
   Otherwise scroll positions and selections would be forgotten when the
   language changes.
3. **The caller owns the values.** `Checkbox(ctx, label, myBool)` takes the
   application's own `bool`. The context only remembers what the application
   does not need to know: hover, focus, where a drag started.
4. **Colours are palette slots only** (§5), never literals. Then a theme
   change is one file.
5. **A new palette slot goes at the end of `enum Slot`**, with a name in
   `kSlotNames`, a value in all three built-in themes (`Theme.cpp`) and in
   `themes/*.json`. The theme editor and the JSON loader pick it up by
   themselves; older theme files without it stay valid.
6. **The geometry layer is closed** (§2.1). Shapes are drawn only by the
   scrollbar thumb, progress bar and switch, the caption band and its boxes,
   the splitter, the slider, the lines between editable table rows (§8.19),
   chart data (§8.20), diagram edges, nodes and box outlines (§8.21), and the
   dimming under a modal. A new widget is built from glyphs.
7. **An interactive widget asks for the hand pointer.** Under hover:
   `if (ctx.isHot(id) && !dis) ctx.setCursor(ui::Cursor::Pointer);`, as every
   clickable widget of the system does (§9). A picture moved by dragging
   (diagrams, §8.21) asks for `Cursor::Move`.
8. **Overlays can be drawn anywhere in the frame.** Menus, lists and modals
   write into their own cell grid and always end up on top. They do not have
   to be called at the end of the frame.
9. **A table with editable cells is `ui::EditTable`.** The widget edits a cell
   in place through a `std::string*` (`nullptr` makes it read-only). The
   application inserts, deletes and sorts rows on the events `ET_INSERT`,
   `ET_DELETE` and `ET_SORT`; `panelTasks` in `Playground.cpp` is an example.
10. **Whatever moves by itself asks for a frame.** Frames are drawn on demand
    (§9): the loop sleeps until the next event. A widget or screen that
    changes with time (a running bar, a live chart, a clock) calls
    `ctx.animate(seconds until the next change)` every frame; `0` means "as
    soon as possible". The blink, tooltips, the pendulum highlight and the
    glitches ask on their own. A forgotten request does not freeze anything,
    but the element then updates only every 0.25 s.

The full widget reference is `src/UI/Widgets/Widgets.hpp`. Live examples of
every widget are in `src/App/Playground.cpp`.

## 4. What to rename

| What | Where |
|---|---|
| the window caption | `Title()` in the application's `src/App/*.cpp` |
| the project name | `project(ConUI CXX)` in `CMakeLists.txt` |
| executable names (`ConUI.exe` is the launcher, `ConUIApp.exe` the application) | the `ConUI` and `ConUIApp` targets in `CMakeLists.txt`, the path `bin\ConUIApp.exe` in `src/launcher.cpp`, the paths in `build.sh`, `build_macos.sh` and `build_linux.sh` |
| the environment variable `CONUI_APP_ROOT` | `src/launcher.cpp`, `src/Core/Utils.cpp` and the three build scripts |
| the `data.bin` signature (`kMagic`, `"CNUI"`) | `src/Core/Utils.cpp`: change it so the new application never reads another one's `data.bin` |
| the macOS data folder (`Application Support/ConUI`) | `GetDataDirectory()` in `src/Core/Utils.cpp` |
| the icon | `src/icon.ico` (Windows), `AppIcon` in the bundle (macOS) |
| the caption box icons | `src/assets/caption/caption_<N>.png`, N = 8 to 32: a strip of 5 white N × N masks (close, collapse, zoom, fullscreen, filled). Replace them with art of the same layout, or regenerate with `tools/capicons.cpp`; the theme gives them their colour |
| executable metadata | `src/resources.rc` (`CompanyName`, `ProductName`, …) |
| the bundle identifier | `MACOSX_BUNDLE_GUI_IDENTIFIER` in `CMakeLists.txt` (`org.conui.playground`) |
| the `app.title` string | the table in `src/UI/Strings.cpp` |

## 4a. Own resources in `resources.dat`

`resources.dat` is an archive that `packer` builds from the `RES_SRC_FILES`
list in `CMakeLists.txt`: the font and the icon sheets. To add a file, append
it to that list; in code it is fetched by its file name without a path:
`ResourceManager::Get().GetResource("logo.png", n)`. `RasterStore::addFromMemory()`
puts a picture from those bytes into the raster layer (§16). A picture the
application draws itself and changes every frame is added once (`addRGBA`)
and then redrawn with `RasterStore::update()`; adding a new raster every frame
would quickly fill the page.

**Own icons** (for a sidebar, §8.18) are a family of PNG strips
`<prefix>_<N>.png`, one file per pixel size, `count` N × N squares in a row,
white on transparency. Put them into `src/assets/<folder>/` (they go into
`resources.dat` automatically; names must be unique), generate them as
`tools/secicons.cpp` does, or draw them. In a frame:

```cpp
const IconStrip* ic = LoadIconStrip(s.rasters, "myicons", ctx.input().cell_h, 3);
ui::SidebarItem items[3] = { { ic ? ic->ids[0] : -1, str::T(str::ID_MY_A) }, /* … */ };
ui::Sidebar(ctx, "nav", ui::Rect{ 0, top, ui::SIDEBAR_W, bodyH }, items, 3, mySection,
            ic ? ic->px : 0,
            1.0f - ui::RULE_MID, ui::RULE_MID);   // reach the ─ rules above and below
```

The last two arguments are needed only when `─` rules run above and below the
sidebar: its ground then reaches the middle of their stroke. At the window
edge pass `0, 0` (the default).

A sheet of the needed size is loaded once and cached; without an exact size,
the nearest one is used at its native size.

## 5. Own state in `data.bin`

**Application state** (the open screen, a tab, field contents) is returned
from `app::SaveState()` as one string and handed back to `app::LoadState()` on
the next start. The format is up to the application. The playground writes
JSON with the bundled parser (`src/Core/Json.hpp`): unknown keys are skipped,
missing ones keep their defaults, so files from older and newer versions read
equally well. Check every value read for its range, as
`LoadPlaygroundState()` does.

The core saves the theme, the locale, the window and section opacity
(`opacity`, `panel_opacity`; the sliders for them belong to the application),
the density, **the geometry of both windows** and the split fractions. The
autosave runs every 5 seconds, only when something changed, and on exit. The
write is atomic, through `data.bin.tmp`, so an interrupted write never damages
the previous file.

**A core field** is appended to the end of `AppState` (`src/Core/Utils.hpp`)
and to the end of `serialize_app_state_impl()` / `deserialize_app_state_impl()`
(`src/Core/Utils.cpp`). An older, shorter file leaves the new fields at their
defaults. The version number does not need to change while fields are only
appended.

**Where the files are.** On Windows everything is in one folder with the
launcher: portable, and copying the folder copies the settings. On macOS
`resources.dat` is in the bundle's `Contents/Resources`, while `data.bin` and
`themes/` are in `~/Library/Application Support/ConUI/`, because a signed bundle
cannot be written to. See `GetResourceDirectory()` and `GetDataDirectory()` in
`src/Core/Utils.hpp`.

## 6. A second window

Return a string from `SecondTitle()`, a size from `SecondSize()`, and fill
`SecondFrame()`. The window opens with `a.open_second = true` and closes with
`a.close_second = true` or `Esc`. The GL context and the atlas are shared
with the main window. The playground's theme editor
(`src/App/ThemeEditor.cpp`) is an example; [THEME_EDITOR.md](THEME_EDITOR.md)
shows how to reuse it.

## 7. What the core does by itself

An application frame does not deal with any of this:

- `Esc` and `Ctrl+Q` in the main window close the application, unless a text
  field or an open menu took the key. `F11` toggles fullscreen: no caption,
  and the window edges cannot be dragged;
- the window redraws while an edge is being dragged, not only after the
  button is released (the frame is also produced from the GLFW refresh
  callback);
- glitches (§9): a text field in error jolts by itself, and so does a
  dangerous modal. An application shows its own failure with
  `ctx.glitchScreen(ui::Context::ScreenGlitch::Tear)`, and connects its own
  widget with `ui::Glitch(ctx, id, rect, error)` after drawing it. The user's
  switch is `AppState::glitches`; the checkbox for it belongs to the
  application;
- everything in section 5 is saved automatically, every 5 seconds and on
  exit. Windows reopen where they were closed, and if that monitor is gone,
  they come back into the work area of the nearest one;
- `build.sh` checks that every DLL the executables need is either in `bin/`
  or part of Windows: a new dependency breaks the build, not a user's start;
- a density change rebuilds the atlas and the grids of both windows on the
  fly.

## 8. Tests

`--selftest` runs on every `build.sh` build and checks the core. Add checks to
`src/App/SelfTest.cpp`. Sections tied to a particular application are wrapped
in `#ifdef CONUI_PLAYGROUND`; a new application can define its own
`CONUI_<NAME>` the same way. `CellBuffer::dump()` produces text, so a screen
test is a comparison of strings, without screenshots. The playground's
screens have text baselines in `tests/snapshots/`; after an intended visual
change, run the self-test once with `CONUI_SNAPSHOT_UPDATE=1` to rewrite them.

## 9. Adding ConUI to an existing application

ConUI is a C++20 framework that **owns the window and the main loop**: the
core opens the OS window, polls input, calls the application's frame
functions and draws. It is not a widget library that plugs into another
toolkit's loop. That decides how it fits into an existing program.

**An existing C++ program without a GUI, or with one that is being
replaced.** The program becomes a ConUI application:

1. Start from the skeleton (sections 1 and 2) and keep ConUI's `main.cpp`.
2. Build the existing code as a library target and link it into `ConUIApp`
   in `CMakeLists.txt` (`add_subdirectory` or `add_library`, then
   `target_link_libraries(ConUIApp PRIVATE mycore)`).
3. Initialise it in `app::Init()`, call it from `app::MainFrame()`, and keep
   its settings in `SaveState()` / `LoadState()`.
4. Run long work on a worker thread. The frame only reads results. While work
   is pending, call `ctx.animate(0.05)` so the UI keeps polling; frames are
   otherwise drawn only on input.

**A program in another language** (Python, Node, Rust, Go, …). ConUI has no
bindings. Two practical routes:

- run a ConUI application as the front end and talk to the existing program
  over a pipe, a local socket or HTTP. The ConUI side stays a normal
  application from the skeleton; the protocol is the application's choice;
- or adopt ConUI as a **design language** in the program's own UI stack: the
  palette, the cell grid, the glyph vocabulary and the component rules of
  this document carry over to a web UI, a terminal UI or another toolkit. The
  Claude Code skill in [`skill/`](../skill/README.md) has a reference for
  that port (`skill/conui/references/porting.md`).

**A program that already owns an OpenGL loop.** Not packaged. The pieces are
separable (`CellBuffer`, `ui::Context::begin/end`, `FontAtlas`, `Renderer::draw`),
and `src/main.cpp` shows how they are wired, but embedding them in a foreign
loop means writing that wiring.
