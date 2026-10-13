# ConUI

**ConUI** (Console UI) is a C++20 framework and design system for desktop
applications that look like a terminal: a native OS window whose entire
interface is a grid of character cells, with box-drawing frames, inverted
selection, one accent colour and themes in JSON. It is not a terminal
emulator and not a TUI library: it is an ordinary desktop application with a
GPU renderer, mouse and keyboard input, OS windows, transparency and HiDPI
support.

A ready-to-run Windows build of the playground is in
[`release/`](release/README.md).

## Features

- **A cell-grid UI** rendered with OpenGL in **one draw call per frame**:
  glyphs from JetBrains Mono, scaled so block and box-drawing characters meet
  without seams at any size.
- **Immediate-mode widgets**: panels, buttons, checkboxes, radios, switches,
  sliders and range sliders, progress bars, single and multi-line text
  editing, tables (sortable, editable), trees, tabs, dropdowns, context menus
  and menu bars, modals, tooltips, a sidebar, line and bar charts, tree and
  network diagrams.
- **A layout of split trees** with draggable seams, saved between runs.
- **Themes**: 32 named palette slots, three built-in themes, theme files in
  JSON, and a live **theme editor** in its own OS window.
- **Native window behaviour**: the OS frame, Snap, shadow and resize edges,
  with a flat System 7 style caption drawn by ConUI; fullscreen on `F11`;
  window and panel transparency.
- **Localisation** with a width budget per string, checked at build time
  (English and Russian included).
- **Frames on demand**: an idle application uses almost no CPU.
- **Testable rendering**: the screen dumps to text, and the self-test compares
  it with baselines, without screenshots.
- **One third-party library** (GLFW) plus two vendored stb headers.

## What is in this repository

| Part | Where | What it is |
|---|---|---|
| **Design system** | [`docs/DESIGN_SYSTEM.md`](docs/DESIGN_SYSTEM.md) | the reference: render model, grid, glyphs, palette, every widget, states |
| **Core** | `src/Core`, `src/Render`, `src/UI` | windows, rendering, the immediate-mode context, layout, widgets, themes, strings |
| **Playground** | `src/App/Playground.*` | the showcase application: every widget in every state, charts, diagrams, the theme editor |
| **Skeleton** | `src/App/Skeleton.cpp` | an empty application to start from |
| **Theme editor** | `src/App/ThemeEditor.*`, [`docs/THEME_EDITOR.md`](docs/THEME_EDITOR.md) | live palette editing in a second OS window |
| **Claude Code skill** | [`skill/`](skill/README.md) | instructions that let Claude Code build ConUI applications |

## How it works

**The screen is a grid of cells.** Each cell holds a codepoint, a foreground
and background colour (as palette slot indices, never RGB) and attributes.
Widgets write into the grid; layout is counted in whole cells. A short,
closed list of elements that cannot be whole cells (scrollbar thumbs,
progress fills, slider lines, chart data, diagram edges) goes into a
separate geometry layer, in cell fractions.

**Layers, one draw call.** Window background, panel backgrounds, geometry,
raster pictures, cell backgrounds, overlays and glyphs are separate layers
with their own opacity. One pass turns them into one vertex stream; the
shader picks between the glyph atlas and the raster page per vertex. Menus,
lists and modals write into a second cell grid, so they are always on top
regardless of call order.

**Immediate mode.** An application describes its whole screen every frame,
and user actions come back as return values:

```cpp
void MainFrame(ui::Context& ctx, AppServices& s, AppActions& a) {
    const int W = ctx.cells().width(), H = ctx.cells().height();
    const int top = s.fullscreen ? 0 : chrome::CONTENT_TOP;
    ui::Rect left, right;
    ui::Split(ctx, s.layout, "main", ui::Axis::Cols,
              ui::Rect{ 1, top, W - 2, H - 2 - top }, 0.4f, 20, 30, left, right);

    ui::BeginPanel(ctx, "tasks", str::T(str::ID_TASKS), left);
    if (ui::Button(ctx, str::ID_RUN, ui::BTN_DEFAULT)) startTask();
    ui::Checkbox(ctx, str::T(str::ID_OPT_AUTOSAVE), settings.autosave);
    ui::EndPanel(ctx);

    if (quitRequested) a.quit = true;
}
```

The core owns the window, the loop, the renderer, themes and the settings
file; an application is a handful of functions (`src/App/App.hpp`). Frames
are drawn when input arrives or when something animated asks for one.

## Platforms

| Platform | Build | Status |
|---|---|---|
| Windows 11 (MSYS2 UCRT64, MinGW-w64) | `build.sh` | built, tested, used |
| macOS 11+ (Apple Silicon and Intel) | `build_macos.sh` | build script, CMake setup and platform layer are prepared; **not yet run on real hardware** |
| Linux (X11 or Wayland) | `build_linux.sh` | build script, CMake setup and platform layer are prepared; **not yet run**. The window keeps the desktop's title bar |

The code is portable C++20 over GLFW and OpenGL 3.3. Platform specifics are
isolated in `src/Core/Platform_win.cpp`, `Platform_mac.mm` and
`Platform_other.cpp` (Linux and the BSDs), plus the executable path lookup in
`src/Core/Utils.cpp`. Reports and fixes from macOS and Linux are welcome.

## Building

### Windows

1. Install [MSYS2](https://www.msys2.org/) and open the **MSYS2 UCRT64**
   shell.
2. Install the toolchain and GLFW:

   ```bash
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
                      mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-glfw git
   ```

3. Clone and build:

   ```bash
   git clone https://github.com/mindscanlabs/ConUI.git
   cd ConUI
   ./build.sh            # the playground → dist/win/
   ./build.sh run        # build, then launch
   ./build.sh package    # build, then pack dist/win into release/ConUI-win-x64.zip
   ```

`build.sh` configures with CMake and Ninja, compiles, runs the self-test,
stages a ready-to-run folder in `dist/win/` and checks that every DLL the
executables need is shipped. It closes running `ConUI.exe` and `ConUIApp.exe`
first. Start the result with `dist/win/ConUI.exe`; the folder is portable.

The skeleton builds the same way: `CONUI_APP=skeleton ./build.sh` →
`dist/win-skeleton/`.

### macOS

```bash
./build_macos.sh                       # the playground → dist/macos/ConUIApp.app
CONUI_APP=skeleton ./build_macos.sh    # the skeleton  → dist/macos-skeleton/
```

Requires Homebrew and the Xcode command line tools; the script installs
`glfw`, `cmake`, `ninja`, `imagemagick` (for the icon) and `dylibbundler`,
runs the self-test, bundles the dylibs and signs the bundle ad hoc. Settings
and themes go to `~/Library/Application Support/ConUI/`.

### Linux

Install a C++20 compiler, CMake, Ninja (optional), GLFW and the OpenGL
headers:

```bash
sudo apt install build-essential cmake ninja-build libglfw3-dev libgl1-mesa-dev   # Debian, Ubuntu
sudo dnf install gcc-c++ cmake ninja-build glfw-devel mesa-libGL-devel            # Fedora
sudo pacman -S base-devel cmake ninja glfw                                        # Arch
```

Then:

```bash
./build_linux.sh                       # the playground → dist/linux/
CONUI_APP=skeleton ./build_linux.sh    # the skeleton  → dist/linux-skeleton/
./build_linux.sh run                   # build, then launch
```

The self-test opens a hidden window, so it needs a graphical session (or
`xvfb-run ./build_linux.sh`); `CONUI_SKIP_SELFTEST=1` skips it. Start the
result with `dist/linux/ConUIApp`; the folder is portable, like on Windows.

### Build options

| Option | Default | Effect |
|---|---|---|
| `CONUI_APP` | `playground` | which application to build: `playground` or `skeleton` (or an added one) |
| `CONUI_ABOUT_CAT` | `ON` | the animated cat in the playground's ABOUT box. It demonstrates a picture an application draws itself and is not part of the design system; `OFF` leaves its code out |

The build scripts read both from the environment
(`CONUI_ABOUT_CAT=OFF ./build.sh`); with CMake directly, pass them as
`-DCONUI_APP=skeleton -DCONUI_ABOUT_CAT=OFF`.

## Using the playground

The playground shows every part of the system. Switch screens with the
**VIEW** menu or the sidebar on the left:

| Screen | Shows |
|---|---|
| Workspace | a split-tree layout: controls in tabs, build progress, a file tree, an editable table, a process table, a text editor |
| States | every widget in every state side by side: idle, hover, pressed, focused, disabled, error |
| Glyphs | the glyph vocabulary and the box-drawing and block ranges of the font |
| Charts | line and bar charts, one of them live |
| Diagrams | an expression tree and a neural network, with zoom and pan |

Keys: `Tab` moves the focus, `F1` opens ABOUT, `F11` toggles fullscreen,
`Ctrl+Q` or `Esc` quits. The **THEME** menu switches themes and opens the
theme editor ([guide](docs/THEME_EDITOR.md)). The SETTINGS tab has the
language, the density (cell size), window and panel opacity, and the switch
for glitch effects.

## Building an application

Start from the skeleton: copy `src/App/Skeleton.cpp`, add it as a target in
`CMakeLists.txt`, and write the window body in `MainFrame()`. The full guide,
including what to rename, how to keep state, a second window and adding ConUI
to an existing program, is [`docs/FORK.md`](docs/FORK.md). The widget
reference is `src/UI/Widgets/Widgets.hpp`, and the playground has a live
example of every widget.

## Claude Code skill

[`skill/`](skill/README.md) contains a skill for
[Claude Code](https://claude.com/claude-code). After installing it, Claude can
create ConUI applications, add ConUI to existing ones, port the design
language to other stacks and make themes on request, following the design
rules:

```powershell
.\skill\install.ps1          # Windows
```

```bash
./skill/install.sh           # macOS, Linux, Git Bash
```

See [`skill/README.md`](skill/README.md) for details and examples.

## Documentation

| Document | Contents |
|---|---|
| [Design system](docs/DESIGN_SYSTEM.md) | principles, render model, cell geometry, glyphs, palette, every widget, states, files, dependencies |
| [Building applications](docs/FORK.md) | the skeleton, the application contract, rules, renaming, state, integration |
| [Theme editor](docs/THEME_EDITOR.md) | editing, saving and sharing themes; the palette slots |
| [Claude Code skill](skill/README.md) | installing and using the skill |

## Tests

`ConUIApp.exe --selftest` runs on every build: font coverage, seamless
glyphs at every cell size, the render layers, JSON and theme files, string
widths in every locale, widget behaviour, and text snapshots of the
playground screens in `tests/snapshots/`. Run it by hand from the build
folder:

```bash
cd build              # Windows: the app is in bin/
CONUI_APP_ROOT="$(pwd)" CONUI_SNAPSHOT_DIR=../tests/snapshots ./bin/ConUIApp.exe --selftest

cd build_linux        # Linux: the app sits in the build folder
CONUI_APP_ROOT="$(pwd)" CONUI_SNAPSHOT_DIR=../tests/snapshots ./ConUIApp --selftest
```

After an intended visual change, add `CONUI_SNAPSHOT_UPDATE=1` once to
rewrite the baselines.

## License

ConUI is released under the [MIT License](LICENSE).

Bundled third-party files keep their own licences:

- JetBrains Mono (`src/fonts/JetBrainsMono-Regular.ttf`): SIL Open Font
  License 1.1, see [`src/fonts/OFL.txt`](src/fonts/OFL.txt);
- `stb_truetype.h` and `stb_image.h` (`src/Core/`): public domain or MIT, as
  stated in each header.
