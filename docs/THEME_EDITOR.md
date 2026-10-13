# Theme editor

The theme editor changes the colours of a ConUI theme while the application
runs, and saves them as a JSON file. Every edit shows at once, in the
editor's preview and in the main window.

## Opening and closing

The editor ships with the playground. It is a second OS window, not a panel,
so it can sit on another monitor next to the application.

- Open it from the **THEME** menu (`Theme editor…`) or with the
  **THEME EDITOR…** button on the SETTINGS tab of the WIDGETS panel.
- On the very first run it opens by itself. After that, the playground
  remembers whether it was open and where it stood.
- Close it with `Esc` (unless a text field or the theme list holds the key)
  or with its close box.

## Layout

```
 ▫                   THEME EDITOR                    ▭ ▫ ⊿

  [ NEW ] ◆ SAVE ◆ [ REVERT ]                         ← toolbar
 ───────────────────────────────────────────────────
  THEME  ▌ Phosphor                            ▼▌     ← the theme being edited
  FILE   themes/phosphor.json                ✓ saved  ← where it is saved
 ───────────────────────────────────────────────────
  SLOT              HEX  ┊ [ bg.panel ]               ← the selected slot
  ██ bg.window   #062038 ┊   ██████████   #0B3055
 ▐██ bg.panel    #0B3055▌┊  R ├■──────────────┤  11   ← one slider per channel
  ██ bg.raised   #124070 ┊  G ├───■───────────┤  48
  …                      ┊  B ├──────■────────┤  85
                         ┊  A ├──────────────■┤ 255
                         ┊  HEX ▌#0B3055        ▌     ← the same colour as text
                         ┊  ─ LIVE PREVIEW ─────────
                         ┊  ┌─[ PANEL ]────────────┐
                         ┊  │ …                    │
 ───────────────────────────────────────────────────
  32 SLOTS  3 changed            Ctrl+S save · Esc close
```

The seam between the slot list and the slot editor can be dragged; a double
click returns it to its default position.

## Editing a colour

1. **Pick a theme** in the THEME list. The main window switches to it as
   well: the editor always edits the theme the application is drawn with.
2. **Pick a slot** in the list on the left: click it, or focus the list and
   use `↑`/`↓`, `Home`/`End`. The wheel scrolls the list. Each row shows the
   slot's colour (`██`, drawn in that very colour), its name and its hex
   value.
3. **Change the colour**:
   - drag the **R**, **G**, **B** and **A** sliders, use the wheel over them,
     or focus one and press `←`/`→` (`Shift` for a fine step);
   - or type into **HEX**: `#RRGGBB`, or `#RRGGBBAA` with alpha. While the
     text is not a valid colour, the field turns red, jolts once, and the
     colour is left as it was. It applies as soon as the text is valid.

The **A** (alpha) channel matters for the slots that can be translucent:
`shadow` (the dimming under a modal) and the backgrounds. For text, frames
and accents, keep it at 255: glyphs are always drawn opaque.

The **LIVE PREVIEW** under the sliders is drawn with the palette being
edited: a panel, primary text, a selected row, a button, a progress bar, the
retro bar (`retro.filled`, `retro.empty`), the dither pattern and `danger`.
Slots it does not show (the sidebar, tables, charts) are visible in the main
window, which changes in step.

## Saving

| Action | What it does |
|---|---|
| **SAVE** or `Ctrl+S` | writes the theme to its file. The FILE line switches from `● modified` to `✓ saved`, and the status bar reports the result |
| **REVERT** | returns every slot to the last saved version. Disabled while nothing has changed |
| **NEW** | makes a copy of the current theme under a new name (`Phosphor 2`, `Phosphor 3`, …) and switches to it. The copy exists only in memory until it is saved, and reads as modified until then |

The status bar counts the slots that differ from the saved version
(`3 changed`).

### Where files go

Themes live in the `themes/` folder next to the application (on macOS, in
`~/Library/Application Support/ConUI/themes/`).

- A theme loaded from a file is saved back to that file.
- A new theme is saved to a file named after it: lower case, with anything
  other than letters, digits, `-` and `_` replaced by `-`. `Phosphor 2`
  becomes `themes/phosphor-2.json`.
- Saving a built-in theme (Phosphor, Paper, Midnight) writes
  `themes/<name>.json`. A file whose `name` matches a built-in replaces the
  built-in at the next start, so a retuned Phosphor stays retuned. To go back
  to the original, delete the file.

### Renaming a theme

The editor has no rename field. Open the JSON file in any text editor and
change `"name"`; the file name can stay as it is. The new name shows after
the next start.

## The palette

Every colour in ConUI is one of 32 named slots; widgets never use colour
literals. The slots and their roles:

| Slots | Used for |
|---|---|
| `bg.window`, `bg.panel` | the window and section backgrounds (layer colours, see Design System §2) |
| `bg.raised`, `bg.sunken`, `bg.selection` | raised surfaces (headers, popups), input fields, selection |
| `border.dim`, `border`, `border.focus` | frames: inactive, normal, focused |
| `text.primary`, `text.secondary`, `text.muted`, `text.inverse` | text by importance; `text.inverse` sits on `bg.selection` |
| `accent`, `accent.dim` | the single accent: progress, sliders, scrollbars, titles |
| `warn`, `danger`, `success`, `info` | states. `danger` is for irreversible actions and errors only |
| `shadow` | the dimming under a modal; its alpha is the dimming strength |
| `dither` | dot patterns and the tracks of bars and switches |
| `retro.filled`, `retro.empty` | the ticks of the retro progress bar `[■■■□□□]` |
| `sidebar.bg`, `sidebar.icon`, `sidebar.hover`, `sidebar.active` | the sidebar ground and its icon states |
| `table.colsep`, `table.rowsep` | column separators, and the line between editable table rows |
| `chart.1` … `chart.4` | chart series, in order; also the positive and negative weights of network diagrams |

Guidelines that keep a theme readable:

- Keep `text.primary` on `bg.panel` at a strong contrast; `text.muted` is for
  hints and should still read.
- `text.inverse` must read on `bg.selection`: selected rows, pressed buttons
  and the focused cell of a table use that pair.
- Reserve the most alarming colour for `danger`. In the built-in themes it is
  the only red (or magenta) on screen.
- Make `chart.1` to `chart.4` distinct from each other and from `danger`.

## Window and section opacity

The transparency of the window background and of the section backgrounds is
not part of a theme. It belongs to the window and is set with the OPACITY and
PANELS sliders on the WIDGETS tab. A theme can still make a slot translucent
through its alpha channel (the shipped `onyx.json` does this for its panels).

## The file format

A theme file is plain JSON:

```json
{
  "name": "Phosphor",
  "author": "ConUI",
  "version": 1,
  "colors": {
    "bg.window": "#062038",
    "bg.panel":  "#0B3055",
    "shadow":    "#0000008C"
  }
}
```

Keys are the slot names from the table above. A slot missing from the file
comes from the built-in theme of the same name, otherwise from Phosphor, so
a file with a single colour is valid. A broken file is skipped and reported,
and never stops the application from starting. The full rules are in the
[Design System, §15](DESIGN_SYSTEM.md#15-themes-as-files).

## Adding the editor to an application

The editor is ordinary application code built from ConUI widgets:
`src/App/ThemeEditor.cpp`, with one entry point:

```cpp
EditorResult BuildThemeEditor(ui::Context& ctx, ui::Layout& lay, ThemeEditorState& st,
                              ThemeStore& themes, int themeIndex, int top);
```

To give an application built from the skeleton its own theme editor, use the
second window of the core (see [FORK.md](FORK.md), "A second window"):

```cpp
#include "ThemeEditor.hpp"

static ThemeEditorState g_editor;

const char* SecondTitle() { return "THEME EDITOR"; }
void SecondSize(int& cols, int& rows) { cols = 60; rows = 34; }

void SecondFrame(ui::Context& ctx, AppServices& s, AppActions& a) {
    const EditorResult e = BuildThemeEditor(ctx, s.layout, g_editor, s.themes,
                                            s.themeIndex, chrome::CONTENT_TOP);
    if (e.select_theme >= 0) a.theme = e.select_theme;
    if (e.close) a.close_second = true;
}
```

Open it from a button or a menu with `a.open_second = true`, and add
`src/App/ThemeEditor.cpp` to the application's sources in `CMakeLists.txt`.
