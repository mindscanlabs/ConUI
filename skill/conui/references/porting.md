# Porting the ConUI design language to another stack

Use this when the target program is not C++ (a web app, Electron, a Python or
Go terminal UI, Qt, a game UI) and the user wants it to look and behave like
ConUI. The code of ConUI is not reused; its rules are. Read the matching
sections of `docs/DESIGN_SYSTEM.md` for each component being ported.

## 1. Tokens from the theme files

Each `themes/*.json` in the ConUI repository is a complete token set: 32 named
colours (§5, §15). Turn them into the target's token format, one set per
theme, and keep the slot names.

CSS custom properties:

```css
:root[data-theme="phosphor"] {
  --bg-window: #062038;   --bg-panel: #0B3055;   --bg-raised: #124070;
  --bg-sunken: #03182A;   --bg-selection: #1FA35C;
  --border-dim: #1B5585;  --border: #E8C14A;     --border-focus: #FFE07A;
  --text-primary: #EAF2FA; --text-secondary: #9CC4E4; --text-muted: #5E87AC;
  --text-inverse: #03182A;
  --accent: #4AE8E8;      --accent-dim: #2A8C9C;
  --warn: #E8C14A;        --danger: #FF6B6B;     --success: #7CE89A;  --info: #8AC8FF;
  --shadow: #0000008C;    --dither: #124070;
  --chart-1: #4AE8E8;     --chart-2: #E8C14A;    --chart-3: #7CE89A;  --chart-4: #C49AFF;
}
```

`#RRGGBBAA` values carry alpha. Missing slots in a theme file take the value
of the built-in theme of the same name (`src/UI/Theme.cpp`), otherwise
Phosphor.

Components refer only to tokens, never to raw colours. Switching themes is
switching the token set.

## 2. The grid

- **One monospace font, one size.** JetBrains Mono at 15 px is the reference
  (`src/fonts/`, OFL licence). Line height = 1.32 em, so a cell is 0.6 em by
  1.32 em (9 × 20 px at 15 px). In CSS: `font: 15px/1.32 "JetBrains Mono",
  monospace;` and `ch` units for widths.
- **Lay out in cells.** Widths in `ch`, heights in line-height multiples;
  margins of one cell. In a terminal UI this is native.
- **No proportional text, no second font size.** Hierarchy comes from
  inversion, upper case and the three text brightnesses (§7).

## 3. Frames and surfaces

- Panels are drawn with box-drawing characters or with 1 px borders that
  imitate them: `┌─[ TITLE ]──── status ─┐`. The title sits in the top border,
  in `accent`, in square brackets; a status or count sits at the right end of
  the same border (§8.1, §8.8).
- One frame weight everywhere. No double lines, no shadows, no rounded
  panels, no gradients (§4, §13).
- Layers: window background, panel background, then content. Panel and window
  opacity are window settings, not theme colours (§6).
- Neighbouring panels are separated by a one-cell seam that can be dragged;
  the layout is a split tree, not floating panels (§8.17).

## 4. Components

| ConUI | Port |
|---|---|
| Button `[ RUN ]` | text in brackets; hover swaps to `▐ RUN ▌` or inverts; pressed is inverted; the default action has `◆ RUN ◆`; danger is `danger` text |
| Checkbox `[✓]`, radio `(●)` | glyph controls with a text label; no native widgets |
| Switch | a short pill track in `dither` with a thumb, `success` when on (§8.5) |
| Progress | a smooth bar on a `dither` track with an `accent` fill; the retro variant `[■■■□□□]` for log-like places (§8.3) |
| Slider | a thin line with end caps and a square knob; the filled part in `accent` (§8.4) |
| Input | a `bg.sunken` strip, block cursor `█`, error in `danger` with `✗` (§8.12) |
| Table | header on `bg.raised`, `│` column separators in `table.colsep`, no rule under the header, numbers right-aligned, titles left-aligned, sort arrow `▲▼`, full-width inverted selection (§8.8) |
| Tree | `├── └── │` connectors, `▼ ▶` folding, full-width inverted selection, child count on collapsed folders (§8.7) |
| Tabs | the active tab is the panel title; inactive tabs break out of the frame above it (§8.15) |
| Menu, dropdown, tooltip | a framed box on `bg.raised`, no shadow, inverted item under the pointer, accelerators right-aligned (§8.6, §8.10, §8.11) |
| Modal | single frame, centred, the window behind dimmed by a solid translucent `shadow` layer; a destructive one has a `danger` frame and CANCEL as default (§8.9) |
| Scrollbar | a thin rounded thumb in `accent.dim`, no arrows, no track, visible only while the region is hovered or focused (§8.14) |
| Status bar | the last row: an inverted state badge on the left, metrics on the right, a rule above (§8.1) |
| Charts | frame and axes in text characters; data as thin vector lines and bars in `chart.1..4`; legend `■ name` in the top border (§8.20) |

## 5. States

| State | Look |
|---|---|
| hover | `bg.raised` or the bracket swap; pointer cursor on clickable things |
| pressed | inversion |
| focused | `border.focus` edges and a blinking `◆` in place of the first character |
| selected | inversion across the full row |
| disabled | `text.muted`, no input |
| error | `danger` with `✗`, and an optional one-off glitch: rows shift by 1 or 2 cells and some characters turn into `░▒▓█▀▄` for about 0.2 s; never a standing effect, and switchable off |

## 6. What not to bring along

- Native platform widgets (system checkboxes, select boxes, scrollbars).
- Icons from icon fonts or image sets inside controls: use the glyph
  vocabulary (§4). Raster images are for content (previews, logos).
- Animations other than the cursor blink, the busy bar and the error glitch.
- More than one accent colour.

## 7. Checking the result

Compare a screen with the playground side by side (build it from the ConUI
repository and run `dist/win/ConUI.exe`, or the ready build in `release/`). Then go
through `references/design-rules.md`, skipping the C++-specific items.
