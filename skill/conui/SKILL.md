---
name: conui
description: Build desktop applications with ConUI, a C++20 GUI framework and design system that draws a native window like a terminal (a cell grid, box-drawing frames, a 32-slot themable palette, immediate-mode widgets). Use when the user asks to create an application with ConUI, to add ConUI to an existing application, to use ConUI as the design system for an interface, to add screens or widgets to a ConUI application, or to make a ConUI theme.
---

# ConUI

ConUI is a C++20 framework and a design system in one repository. The core
owns an OS window and the main loop, draws everything as a grid of
character cells with one draw call per frame, and calls the application's
frame functions in immediate mode (`if (ui::Button(ctx, "OK")) …`). The look
comes from the design system: box-drawing frames, inversion instead of
highlight, one accent colour, a palette of 32 named slots, a closed glyph
vocabulary.

## 1. Locate the ConUI repository

Everything below refers to files in the ConUI repository (`CONUI` below).

1. Read the file `CONUI_HOME` next to this `SKILL.md`. The installer writes
   the absolute path of the repository there.
2. Otherwise use the environment variable `CONUI_HOME`.
3. Otherwise check whether the current project is itself a ConUI checkout or
   a ConUI application: it has `src/UI/Widgets/Widgets.hpp` and
   `src/App/App.hpp`.
4. Otherwise ask the user where the repository is, or offer to clone it:
   `git clone https://github.com/mindscanlabs/ConUI.git`.

Before writing code, read these files from `CONUI` (they are the source of
truth; this skill is a map):

- `docs/FORK.md`: how an application is built on the core, the rules, what to
  rename, state, second window, integration;
- `src/App/App.hpp`: the whole contract between core and application;
- `src/App/Skeleton.cpp`: the empty application to copy;
- `src/UI/Widgets/Widgets.hpp`: every widget and its parameters;
- `references/api.md` in this skill: a condensed API map with patterns.

Read `docs/DESIGN_SYSTEM.md` sections as needed (the section for a widget
before using it in an unusual way, §5 before touching colours, §2.1 before
drawing any shape). `references/design-rules.md` is the checklist.

## 2. Decide which task this is

| The user wants | Go to |
|---|---|
| a new application on ConUI | section 3 |
| ConUI inside an existing C++ program | section 4 |
| ConUI as the design system of a program in another stack (web, terminal, Qt, …) | section 5 |
| a new screen, widget usage or theme in a ConUI application | sections 6 and 7 |

When the request is ambiguous ("add ConUI to my app"), look at the target
project first (language, build system, whether it has a GUI) and pick the
route; ask only if two routes are equally plausible.

## 3. A new application

1. **Check the toolchain.** Windows: MSYS2 with the UCRT64 packages
   `mingw-w64-ucrt-x86_64-gcc`, `-cmake`, `-ninja`, `-glfw`; builds run in the
   UCRT64 shell. macOS: Homebrew and the Xcode command line tools
   (`build_macos.sh` installs `glfw`, `cmake`, `ninja`, `imagemagick`,
   `dylibbundler`). Linux: a C++20 compiler, CMake, GLFW and the OpenGL
   headers (`libglfw3-dev libgl1-mesa-dev` on Debian and Ubuntu), then
   `build_linux.sh`. The macOS and Linux paths are prepared but have not been
   run yet: say so, and fix what the first real build reports.
2. **Create the project.** Copy the ConUI repository into the new project
   folder without `build*/`, `dist/` and `.git/` (or let the user fork it on
   GitHub). The new project is a ConUI fork: core plus one application.
3. **Create the application file.** Copy `src/App/Skeleton.cpp` to
   `src/App/<Name>.cpp`. Add a branch to the `CONUI_APP` choice in
   `CMakeLists.txt`:
   ```cmake
   elseif(CONUI_APP STREQUAL "<name>")
       set(APP_SOURCES src/App/<Name>.cpp)
       set(APP_DEFINES CONUI_<NAME>=1)
   ```
   Make it the default: the `CONUI_APP` cache default in `CMakeLists.txt` and
   `APP="${CONUI_APP:-<name>}"` in `build.sh`, `build_macos.sh` and
   `build_linux.sh`. Keep the playground sources in the repository as a
   reference. The playground's ABOUT cat (`src/App/AboutCat.*`,
   `CONUI_ABOUT_CAT`) is an optional demo, not part of the design system:
   never carry it into a new application.
4. **Rename** what `docs/FORK.md` §4 lists: caption (`Title()`), project
   name, executable names, `CONUI_APP_ROOT`, the `data.bin` magic, the macOS
   data folder, `resources.rc` metadata, the bundle id, `app.title`.
   Changing the `data.bin` magic matters: otherwise the new application reads
   the playground's settings.
5. **Design the screen on the grid** before coding: sketch it in box-drawing
   characters at the default size (`MainSize()`, e.g. 120 × 40 cells), with
   the standard frame of a ConUI window:
   ```
   row 0          caption (drawn by the core; absent in fullscreen)
   row 1          empty
   row top        menu bar (MenuBarItem …), top = chrome::CONTENT_TOP or 0
   row top+1      rule: chrome::DrawRule(cb, top + 1)
   rows top+2..   body: Split tree of panels, optional sidebar at x = 0
   row H-2        rule: chrome::DrawRule(cb, H - 2, /*raised=*/true)
   row H-1        status bar: state on the left, metrics on the right
   ```
   Show the sketch to the user when the layout is not obvious.
6. **Write the frame** in `MainFrame()` with the patterns in
   `references/api.md`. Every user-visible string goes through the string
   table (section 6). Keep application data in the application's own structs;
   widgets take references to it.
7. **Persist** the application's state through `SaveState()` /
   `LoadState()` (JSON with `src/Core/Json.hpp`, tolerant of missing keys).
8. **Build and test**, from the repository root. Windows, in the UCRT64
   shell: `./build.sh` (configures, compiles, runs `--selftest`, stages
   `dist/win/`; it kills running `ConUI.exe`/`ConUIApp.exe` first). macOS:
   `./build_macos.sh` → `dist/macos/`. Linux: `./build_linux.sh` →
   `dist/linux/`. The self-test must pass. Add checks for the application's
   logic to `src/App/SelfTest.cpp` under `#ifdef CONUI_<NAME>`.
9. **Run it and let the user look.** Start `dist/win/ConUI.exe`
   (`dist/linux/ConUIApp`, `dist/macos/ConUIApp.app`) and tell the user
   exactly what to check on screen. Visual correctness is confirmed by the
   user running the application, not by reasoning about screenshots.

## 4. ConUI inside an existing C++ program

ConUI owns the window and the loop; it does not plug into another toolkit's
loop. So the existing program becomes a ConUI application:

1. Set up a ConUI fork as in section 3 (inside the program's repository, for
   example as a `ui/` folder, or with the program's code moved next to it).
2. Turn the existing logic into a library target (`add_library(mycore …)`)
   and link it: `target_link_libraries(ConUIApp PRIVATE mycore)`. Add its
   include paths. Keep the logic free of UI calls.
3. Initialise it in `app::Init()`, drive it from `app::MainFrame()`, store
   its settings with `SaveState()` / `LoadState()`.
4. Long work goes to a worker thread; the frame only reads results (guard
   shared data with a mutex or hand over snapshots). While work is pending,
   call `ctx.animate(0.05)` every frame: frames are otherwise drawn only on
   input.
5. If the program has its own `main()`, ConUI's `src/main.cpp` replaces it as
   the entry point; the old `main()` becomes an initialisation function.

A program that already owns an OpenGL loop and wants ConUI inside it is not
a packaged scenario: `CellBuffer`, `ui::Context::begin/end`, `FontAtlas` and
`Renderer::draw` are separable and `src/main.cpp` shows the wiring, but it
is custom work. Say so before starting.

## 5. ConUI as the design system of another stack

When the program is not C++ (a web app, a Python TUI, Electron, Qt, …), ConUI
is used as a design language, not as code: port the palette, the grid, the
glyph vocabulary and the component rules. Follow `references/porting.md`:
theme JSON to design tokens, a monospace cell grid, frames from box-drawing
characters, inversion for selection, the state rules. Read
`docs/DESIGN_SYSTEM.md` for the components being ported.

If the user wants the real ConUI look with a non-C++ backend, the other
route is a ConUI front-end application talking to the backend over a pipe,
a local socket or HTTP (`docs/FORK.md` §9).

## 6. Strings and localisation

Every label goes through `str::T(str::ID_…)`:

1. Add `ID_<NAME>` to `enum Id` in `src/UI/Strings.hpp`, before `ID_COUNT`.
2. Add a row at the **same position** in `kTable` in `src/UI/Strings.cpp`:
   `{ "key.name", <max width in cells>, "English", "Russian" }`. An empty
   Russian string (`""`) falls back to English.
3. The width budget is real: the self-test fails when a translation is wider.

A region's id is a constant string, separate from its localised title:
`BeginPanel(ctx, "files", str::T(str::ID_FILES), r)`.

## 7. Rules that must hold

The full checklist is `references/design-rules.md`. The ones broken most
often:

- **Colours are slots** (`TEXT`, `ACCENT`, `DANGER`, …), never RGB values.
  `DANGER` only for irreversible actions and errors. One accent.
- **Whole cells.** Layout is in cells (`Rect{x, y, w, h}` in cells). No pixel
  offsets. Shapes (`cb.shape`, `cb.segment`) are reserved for the closed list
  in `DESIGN_SYSTEM.md` §2.1; a new widget is built from glyphs.
- **Glyphs from the vocabulary** (`src/UI/Glyphs.hpp`, §4). No double lines,
  no shadows, no emoji.
- **Selection is inversion**, focus is `border.focus` plus the blinking `◆`.
- **The caller owns the data**: tables report `TBL_SORT`/`ET_INSERT`/… and the
  application sorts, inserts and deletes.
- **Clickable custom elements ask for `Cursor::Pointer`** on hover;
  anything animated calls `ctx.animate(…)`.
- **Upper-case titles and labels** for headings (`PROCESSES`, `RUN`), data as
  is. One font size; hierarchy by inversion, case and brightness.
- **Keyboard first**: every action reachable with `Tab`, arrows, `Enter`,
  `Esc`; destructive confirmations use `MODAL_DANGER` with CANCEL as the
  default button.

## 8. Themes

A theme is `themes/<name>.json` with any subset of the 32 slots
(`docs/DESIGN_SYSTEM.md` §15, `docs/THEME_EDITOR.md`). Missing slots come from
the built-in theme of the same name or from Phosphor. To make a theme: start
from an existing file, keep `text.inverse` readable on `bg.selection`, keep
`danger` the only alarming colour, make `chart.1`..`chart.4` distinct. A new
slot (rare) goes at the end of `enum Slot` with a name in `kSlotNames` and a
value in every built-in theme and theme file.

## 9. Before reporting done

- `./build.sh` passes, including `--selftest`.
- No colour literals, no new shapes outside §2.1, every string in the table
  with a width budget.
- The application was started, and the user was told what to look at.
