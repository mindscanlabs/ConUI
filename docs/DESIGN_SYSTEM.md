# ConUI Design System

The reference for how ConUI looks and behaves: the render model, the cell
grid, the glyph vocabulary, the palette, every widget, the states, and the
rules that keep them consistent. Section numbers are stable: comments in the
source cite them as `§8.14`, `§16` and so on.

Contents

1. [Positioning and principles](#1-positioning-and-principles)
2. [Render model](#2-render-model)
3. [Cell geometry](#3-cell-geometry)
4. [Glyph vocabulary](#4-glyph-vocabulary)
5. [Palette and themes](#5-palette-and-themes)
6. [Transparency](#6-transparency)
7. [Typography and hierarchy](#7-typography-and-hierarchy)
8. [Element anatomy](#8-element-anatomy)
9. [States and interaction](#9-states-and-interaction)
10. [Runtime files](#10-runtime-files)
11. [Project structure](#11-project-structure)
12. [Dependencies](#12-dependencies)
13. [Deliberately out of scope](#13-deliberately-out-of-scope)
14. [Localisation](#14-localisation)
15. [Themes as files](#15-themes-as-files)
16. [Raster elements](#16-raster-elements)

---

## 1. Positioning and principles

ConUI is a **Console UI**: a windowed desktop interface drawn like a
terminal. It is not a terminal emulator and not a TUI library. It is an
ordinary native desktop application whose entire drawing model is a grid of
character cells.

The style formula: **1-bit brutalism × the density and devices of ncurses
tools × the window anatomy of OS/2 and the early Macintosh × modern window
composition (transparency over the desktop).**

Principles:

1. **The grid is the layout coordinate system.** Every element *occupies* a
   whole number of cells. There is no "7 px margin", only "1 cell margin".
   A short, closed list of elements draws finer than a cell inside its own
   rectangle (§2.1).
2. **Inversion instead of highlight.** Selection, focus and the active state
   swap the text and background colours. No glow, no outline.
3. **Glyphs instead of pictures, with raster support.** The system's own
   widgets are drawn with glyphs and rectangles: icons, arrows, markers,
   bars. An application can still show a real picture (a preview, a
   thumbnail, a logo) through a separate raster layer (§16). Of the system's
   own elements, only the window caption boxes (§8.1) and the sidebar icons
   (§8.18) use it.
4. **Colour carries meaning.** The palette is narrow and has one accent. A
   second accent exists only for dangerous, irreversible actions.
5. **Density.** An empty row is rare. Secondary information lives in frame
   lines and corners.
6. **No unnecessary dependencies.** See §12.

---

## 2. Render model

The interface is **layered**. This is how the renderer works, not a
decoration: the window background, the panel background, the panel frame and
the panel's widgets are independent layers, each with its own opacity,
composited bottom up.

```
         ┌────────────────────────────────────────────────────────┐
layer 10 │ overlay raster                                         │
layer 9  │ glyphs, always opaque                                  │
layer 8  │ overlay geometry                                       │
layer 7  │ overlay cell backgrounds: selected menu row, fields    │
layer 6  │ overlay surfaces: menu, tooltip, modal                 │
layer 5  │ dimming under a modal (§8.9)                           │
layer 4  │ window cell backgrounds: selection, fields, headers    │
layer 3  │ raster: sprites, previews, icons (§16)                 │
layer 2  │ geometry: thumbs, bar fills, caption boxes             │
layer 1  │ surfaces: window, panel and caption backgrounds        │
layer 0  │ OS backdrop: the desktop                               │
         └────────────────────────────────────────────────────────┘
```

The window is always drawn **in full**: layers 1 to 4 cover every cell,
including cells hidden under an overlay. This shows where an overlay's
background is cut to its frame line (§8.1): in the half cell outside the
line, what lies under the popup shows through, not a bare window background.

The screen is a two-dimensional array of cells **plus** three ordered lists:
surfaces, geometry and sprites. A cell:

```c
struct Cell {
    uint32_t cp;     // codepoint
    uint8_t  fg, bg; // palette slot indices
    uint8_t  attr;   // BOLD | DIM | INVERSE | UNDERLINE
};
struct Surface { int16_t x, y, w, h; uint8_t slot; uint8_t z; };
```

**`bg.window` and `bg.panel` are layer colours, not element colours.** A cell
that carries one of them paints nothing, because a surface already lies under
it. This rule keeps alpha from being applied twice to the same pixels: a
panel at 94 % sits on a window at 88 %, rather than replacing it. Only
elements paint their own background: `bg.selection`, `bg.sunken`,
`bg.raised`.

`z` is the band: `0` is the window content, `1` is everything floating above
it. Without it, a scrollbar thumb (which lives in the geometry layer, not in
the cells) would draw on top of a modal.

A frame is built like this: widgets write into the cells and into the layer
lists, then one pass turns all of it into one vertex stream. Layer order is
vertex order, not call order, so a frame still costs **one `glDrawArrays`**.
The mode (fill, glyph, geometry, raster) travels in the vertex; the glyph
atlas and the raster page sit on different texture units, and the fragment
shader picks between them. A second draw call would only be needed for a
picture that does not fit in the raster page.

**There are two cell grids as well**, content and overlays, one per `z`
band. Menus, tooltips, dropdown lists and modals write into the second one,
and the screen shows the overlay cell where there is one, the content cell
otherwise. So call order does not matter: a list opened from one panel stays
on top of a neighbouring panel drawn later in the same frame. The dimming
under a modal (§8.9) lies between the content grid and the overlay grid: over
the whole window, whenever it was drawn, but never over the modal itself.

A side effect of this model: **rendering is trivially testable**. The screen
state can be dumped as text and compared with a baseline, without
screenshots. The dump sees only cells; geometry and raster are checked by
their own tests.

### 2.1 The geometry layer: the only exception to the grid

Nine elements do not fit into whole cells, and forcing them in would ruin
exactly what they exist for:

| Element | Why not cells |
|---|---|
| scrollbar thumb | narrow (about half a cell) with rounded ends (§8.14) |
| progress bar fill and switch (same shape, §8.5) | must grow smoothly, not in eighths (§8.3) |
| caption band | a third taller than a row, so not a multiple of the cell (§8.1). The boxes are raster icons (§16); they fall back to geometry only when the icons are missing from the resources |
| splitter grip | a hairline; exactly 2 px under the pointer (§8.17) |
| the whole slider: line, end caps, ticks, knob | the font has no short vertical stroke that meets `─`: `├ ┤` are taller than the cell and merge between sliders in neighbouring rows. Ticks and knobs in whole cells also spaced unevenly as the width changed (§8.4) |
| dimming under a modal | a solid translucent rectangle over the whole window, not a glyph pattern (§8.9) |
| chart data: lines, bars, dashed grid | a value lands on its own pixel, not the nearest cell; a sloped line cannot be drawn with cells at all. A line is its own shape kind, the **segment** (`SHAPE_SEGMENT`: an SDF of a segment with round caps in the same shader). Axes, ticks, labels and the legend are glyphs (§8.20) |
| the line between rows of the editable table | a row of `─` cells would eat a whole row under every record; a 1 px line separates multi-line records without taking space (§8.19) |
| diagrams: edges, network nodes, tree box outlines | edges run at any angle, and when zoomed, nodes and boxes land on fractional positions; box labels are text on whole cells (§8.21) |

They share the geometry layer: a rectangle with a corner radius, filled or
outlined, in **cell fractions** (`x = 2.5` is the middle of the third
column). Fractions, not pixels, so the same numbers work at any DPI and any
density. The exception is what must be a whole number of pixels (the
splitter's 2 px line, the slider's end caps, the width of a seam line): for
those the window reports the cell size in pixels (`ui::Input::cell_w/cell_h`)
and the fractions are computed from it.

This is a deliberate departure from "everything in whole cells", and it is
limited to this table. Buttons, frames, checkboxes, trees, tables and context
menus are still made entirely of glyphs.

---

## 3. Cell geometry

Metrics of `JetBrainsMono-Regular.ttf`, read straight from the font tables:

| Quantity | Value |
|---|---|
| `unitsPerEm` | 1000 |
| advance width | **600** (0.600 em), monospaced |
| ascent / descent | 1020 / -300, lineGap 0 |
| designed line height | **1320** (1.320 em) |
| capHeight / xHeight | 730 / 550 |

The bounding boxes of the block glyphs (`█` U+2588, `▌` U+258C, `▀` U+2580,
`░▒▓`) are **exactly `x[0..600] y[-300..1020]`**: the block characters are
drawn precisely on a `600 × 1320` rectangle. That is the font's own cell:

```
cell_w   = 0.600 × font_px
cell_h   = 1.320 × font_px      → aspect ratio 0.4545
baseline = 1.020 × font_px from the top of the cell
```

Box-drawing characters, on the contrary, **deliberately overhang** the cell:
`│` has the bbox `x[250..350] y[-400..1120]`, `─` has `x[-20..620]
y[320..420]`. That is 20 units horizontally and 100 vertically of margin for
joining. **Consequence for the renderer: a glyph quad must not be clipped to
its cell.**

### Font coverage (checked against the cmap)

| Range | Coverage |
|---|---|
| ASCII `0020-007E` | **full** |
| Latin-1 `00A0-00FF` | full |
| **Box Drawing `2500-257F`** | **128/128, full** |
| **Block Elements `2580-259F`** | **32/32, full** |
| Geometric `25A0-25FF` | 43/96 (`■□▪▫●○◆◇▲▼◀▶` are present) |
| Arrows `2190-21FF` | 35/112 (`←→↑↓↔` are present) |
| Cyrillic `0400-04FF` | 122/256 (basic Russian is present) |
| Braille `2800-28FF` | **none** |
| Legacy Computing `1FB00-` | **none** |

Full Box Drawing and Block Elements coverage is what makes the whole idea
work. Without Braille there are no "dot micro-charts at 1/8 of a cell"; the
eighth blocks take that role (§4). The corner triangles `◢◣◤◥` are missing
as well; `▲▼◀▶` stand in for them.

### The rounding problem and its solution

At `font_px = 15`: `cell_w = 9.0`, `cell_h = 19.8`. If glyphs were rasterised
with one common scale and the cell rounded to 20 px, a 0.2 px gap would open
per cell: visible seams between `█` at every row boundary.

**Solution: everything is rasterised from the font, but the scale is locked
to the cell.** Two independent scales replace the common one, so the font's
own cell lands exactly on the integer pixel cell:

```
sx = cell_w / advance_units      (9 / 600   = 0.03333 px/unit)
sy = cell_h / lineHeight_units   (20 / 1320 = 0.01515 px/unit)
baseline = ascent_units × sy     (fractional, not rounded)
```

The baseline keeps its fractional position, and the glyph is rasterised with
that subpixel phase (`stbtt_MakeGlyphBitmapSubpixel`). `█` then covers the
cell edge to edge at full coverage, and neighbouring cells meet without
seams. The phase is the same for every cell (the cell height is an integer,
the baseline offset inside the cell is constant), so **each glyph is
rasterised once** and cached in the atlas.

The alternative, drawing `2500-259F` as geometry, is seamless too, but it
replaces the type designer's shapes with an approximation: double-line
joints, the `┄` dash pattern and the `░▒▓` textures all come out "similar"
rather than right. The cost of the chosen approach is an `sx/sy` anisotropy
of about 1 % (0.03333 against 0.03409 px/unit at 9 × 20), invisible to the
eye.

**Exception: the light straight frame lines** `─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼ ╴ ╵ ╶ ╷`.
The cell still holds that character, but its atlas image is drawn by the
atlas itself (`FontAtlas::drawBox`), the way terminals do it: exactly one
cell, whole pixels, coverage only 0 or 255. The font's `│` stroke falls
between pixels and overhangs the cell at the top and bottom; where the
overhangs of neighbouring rows overlapped, translucent edges were painted
twice and every joint of a vertical frame showed a faint band. Drawn lines
have no overlap. The stroke width matches the font (100 of the 600 units of
the cell width, `BoxLineIn`), and the horizontal sits where the font's `─`
sits (49 % of the row). Slider and chart tick shapes follow the same rules,
so everything meets pixel for pixel. Rounded, dashed, heavy and double lines
still come from the font; the interface does not use them.

**Consequence:** the font must contain `2500-259F` in full. This is verified
for JetBrains Mono. When the font is replaced, coverage must be checked
again; the self-test checks every glyph of the vocabulary (§4) against the
shipped font.

### Sizes and DPI

The base `font_px = 15` at scale 1.0 gives a **9 × 20** cell (`cell_h` is
rounded to an integer; the glyph scale is locked to the cell, so there are no
seams). The scale comes from `glfwGetMonitorContentScale`. There are four
densities:

| Mode | font_px @1.0 | cell | purpose |
|---|---|---|---|
| Tiny | 11 | 7 × 15 | the most data on screen |
| Compact | 13 | 8 × 17 | a lot of data |
| **Normal** | **15** | **9 × 20** | default |
| Large | 18 | 11 × 24 | HiDPI and accessibility |

`data.bin` stores the density as a number, so Tiny sits at the end of the
enumeration (`Density::Tiny = 3`); the settings list orders densities by
size.

The window resizes freely; the grid is `floor(size / cell)`, and the
remainder along the right and bottom edges is filled with the window
background, as in a terminal.

---

## 4. Glyph vocabulary

The approved set. Widgets take glyphs from here (`src/UI/Glyphs.hpp`) and
nowhere else.

```
Frames              ─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼
Frames, rounded     ╭ ╮ ╰ ╯
Frames, dashed      ┄ ┈ ╌ ╍
Blocks              █ ▀ ▄ ▌ ▐ ▁▂▃▄▅▆▇  ▏▎▍▌▋▊▉
Shades (dithering)  ░ ▒ ▓
Geometry            ■ □ ▪ ▫ ● ○ ◆ ◇ ▲ ▼ ◀ ▶
Arrows              ← → ↑ ↓ ↔
Markers             • · ‣ × ✓ ✗ ~ ≡
Accent brackets     [ ] < > ( ) { }
```

Roles:

- `─ │ ┌…┼`: the default panel frame.
- **There are no double lines `═ ║ ╔…╬`.** The frame weight is the same
  everywhere: window, panel, modal. Hierarchy comes from colour and
  inversion, not line weight (§8.9). The mixed joints `╞ ╡ ╤ ╧ ╪ ╟ ╢` only
  existed to join double lines to single ones, so they are out too.
- **There are no shadows.** Not on buttons, not on floating elements (menus,
  dropdown lists, tooltips, modals). A frame and their own `bg.raised`
  background separate them from the content.
- `╶ ╴`: half lines from the cell centre, the slider ends (§8.4).
- `░ ▒ ▓`: dithering for empty fill and "no data".
- `▏▎▍▌▋▊▉█`: fractional fill left to right.
- `▁▂▃▄▅▆▇█`: fractional fill bottom up (sparklines, small bars).
- `▲ ▼ ◀ ▶`: column sort and tree folding. Scrollbars have no arrows
  (§8.14).
- `● ○`: radio state; `■ □`: retro bar ticks (§8.3) and chart legends
  (§8.20); `✓ ✗`: checkbox; `◆`: a value marker on a scale and the focus
  marker. `■ □` are the largest squares in the font (the full cell width);
  `◼ ⬛ ▣` are not in JetBrains Mono.

---

## 5. Palette and themes

A colour is an **index into the theme palette**, never a literal. The
palette has 32 slots; themes change the values, widgets stay the same.

```
0  bg.window      window background (may be translucent)
1  bg.panel       panel background
2  bg.raised      raised surface (button, table header, popups)
3  bg.sunken      sunken surface (input field)
4  bg.selection   selection and inversion
5  border.dim     frame of an inactive panel
6  border         frame of a normal panel
7  border.focus   frame of a focused window or panel
8  text.primary
9  text.secondary
10 text.muted
11 text.inverse   text on bg.selection
12 accent         the one accent
13 accent.dim
14 warn
15 danger
16 success
17 info
18 shadow         dimming under a modal (the alpha comes from the slot)
19 dither         colour of the dot pattern and of bar tracks
20 retro.filled   filled ■ ticks of the retro bar (§8.3)
21 retro.empty    empty □ ticks of the retro bar
22 sidebar.bg     sidebar background (§8.18)
23 sidebar.icon   sidebar icon at rest
24 sidebar.hover  icon under the pointer
25 sidebar.active icon of the active section
26 table.colsep   column separators │ in table headers and rows (§8.8, §8.19)
27 table.rowsep   the 1 px line between rows of the editable table (§8.19)
28 chart.1        chart series in order: line, bar, ■ in the legend (§8.20)
29 chart.2
30 chart.3
31 chart.4
```

Slots 20 to 31 were appended at the end so earlier numbers never moved. By
default `retro.*` follow `accent.dim` and `text.muted`, `sidebar.*` follow
`bg.raised`, `text.muted`, `text.primary` and `accent`, and `table.*` follow
each theme's `border.dim`. `chart.1` equals `accent`; the other series
colours are picked to stay distinct from each other and away from `danger`
(red and magenta are reserved for danger). A theme file without these keys
is still valid: a missing slot comes from the built-in theme (§15).

### Theme `PHOSPHOR` (default): DOS and ncurses

```
bg.window     #062038    text.primary    #EAF2FA    accent      #4AE8E8
bg.panel      #0B3055    text.secondary  #9CC4E4    accent.dim  #2A8C9C
bg.raised     #124070    text.muted      #5E87AC    warn        #E8C14A
bg.sunken     #03182A    text.inverse    #03182A    danger      #FF6B6B
bg.selection  #1FA35C    border.dim      #1B5585    success     #7CE89A
border        #E8C14A    border.focus    #FFE07A    info        #8AC8FF
shadow        #000000 @55%              dither      #124070
retro.filled  #2A8C9C    retro.empty     #5E87AC
sidebar.bg    #124070    sidebar.icon    #5E87AC    sidebar.hover  #EAF2FA
sidebar.active #4AE8E8
table.colsep  #1B5585    table.rowsep    #1B5585
chart.1 #4AE8E8  chart.2 #E8C14A  chart.3 #7CE89A  chart.4 #C49AFF
```

A yellow frame on blue and a green selection bar, straight from classic
ncurses installers. Cyan as the accent gives a terminal feel without going
neon.

### Theme `MIDNIGHT`: the corporate hacker

```
bg.window     #0B0E12    text.primary    #D7DEE8    accent      #3BE0C6
bg.panel      #11151B    text.secondary  #8A97A8    accent.dim  #1C8E7E
bg.raised     #171D26    text.muted      #545F6D    warn        #E8A33D
bg.sunken     #070A0D    text.inverse    #06090C    danger      #FF3DC8
bg.selection  #3BE0C6    border.dim      #232B36    success     #5BD97E
border        #384454    border.focus    #3BE0C6    info        #5AA9FF
shadow        #000000 @55%              dither      #1A222C
retro.filled  #1C8E7E    retro.empty     #545F6D
sidebar.bg    #171D26    sidebar.icon    #545F6D    sidebar.hover  #D7DEE8
sidebar.active #3BE0C6
table.colsep  #232B36    table.rowsep    #232B36
chart.1 #3BE0C6  chart.2 #5AA9FF  chart.3 #E8A33D  chart.4 #B48CFF
```

A pinch of cyberpunk: `#FF3DC8` (magenta) is reserved **only** for critical,
irreversible actions and failure states, and appears nowhere else. It is the
theme's `danger`: the delete button, the frame of a dangerous modal, the
window close box, a field error and the glitch noise (§9).

### Theme `PAPER`: 1-bit light

```
bg.window #DCDCDC  bg.panel #E6E6E6  bg.raised #F2F2F2  bg.sunken #CFCFCF
text.primary #0A0A0A  text.inverse #F2F2F2  bg.selection #0A0A0A
border #0A0A0A  border.dim #8A8A8A  border.focus #0A0A0A
accent #0A0A0A (inversion instead of colour)  danger #C0142A  dither #A8A8A8
retro.filled #6E6E6E  retro.empty #8A8A8A
sidebar.bg #F4F4F4  sidebar.icon #8A8A8A  sidebar.hover #0A0A0A  sidebar.active #0A0A0A
table.colsep #9A9A9A  table.rowsep #9A9A9A
chart.1 #0A0A0A  chart.2 #6E6E6E  chart.3 #3C3C3C  chart.4 #A8A8A8
```

The three themes above are compiled in and switch at runtime; the chosen
one is stored in `data.bin`. The repository also ships `themes/onyx.json`, a
theme made in the theme editor, as an example of a file-only theme with
translucent panels. Widgets know nothing about themes; they only refer to
slots.

---

## 6. Transparency

**Alpha applies to the cell background; the glyph is always opaque.** Text
stays fully readable over any backdrop.

**Two independent settings for two background layers:**

1. **Window background** (`OPACITY`, 20 to 100 %, default **97 %**)
   multiplies the alpha of the `bg.window` slot **only**.
2. **Section background** (`PANELS`, 0 to 100 %, default 100 %) multiplies
   the alpha of the `bg.panel` slot **only**, in the content band: the panel
   surfaces.

The layers composite bottom up: a section lies on the window, the window on
the desktop. At 0 % sections show the window background; at 20 % window and
100 % sections the desktop shows through the window while the sections stay
solid. **Frames and text are glyphs and always opaque.** Field, selection
and table header backgrounds (`bg.sunken`, `bg.selection`, `bg.raised`)
depend on neither setting. Neither does a modal: its background is
`bg.panel`, but in the overlay band, above the dimming, and it must read at
any setting.

Both values live in `data.bin` (`opacity`, `panel_opacity`), not in the
theme: they are properties of the window, not of the palette. The sliders
for them belong to the application (the playground has them on the WIDGETS
tab).

How it works:

- The window background is one rectangle over the **whole** framebuffer,
  including the remainder on the right and bottom that is not a multiple of
  the cell.
- The alpha channel blends **separately** from colour:
  `glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA)`.
  With one function for all four channels the frame alpha would *shrink*
  under every antialiased edge (`srcA² + dstA·(1-srcA)`), and the compositor
  would show the desktop through thin frame glyphs even at 100 %.

The OS backdrop material (blur, acrylic, mica) is not offered in the
interface: the window always starts with `None`. The `Backdrop` enumeration,
its field in `data.bin` and `platform::SetMaterial` remain, so an
application can bring the choice back if it needs one. OS APIs offer no
continuous blur radius anyway: DWM and `NSVisualEffectView` only switch
between discrete materials.

---

## 7. Typography and hierarchy

One font, one size. Hierarchy comes from four devices:

1. **Inversion**: a heading as a solid bar with text in it. The main device.
2. **Case**: `HEADINGS IN UPPER CASE`, data as is.
3. **Brightness**: `text.primary`, `text.secondary`, `text.muted`.
4. **Letter spacing**: `[ S Y S T E M ]` for large blocks.

Pseudo-bold (`BOLD`) is synthesised by drawing the glyph a second time one
pixel to the right; no separate bold face ships with the system.

Headings never grow in size (the grid does not allow it). A "large" heading,
when needed, is assembled from block characters as ASCII art by an offline
tool, not at runtime.

---

## 8. Element anatomy

All diagrams use real characters, at grid scale.

### 8.1 Window

**The window frame belongs to the OS. The caption is ConUI's own: flat, in
the spirit of Macintosh System 7.** The roles are strictly split: the OS
provides the border, the shadow, the corners, the resize edges, snapping and
the animations; it does not draw a caption at all, because the caption area
is folded into the client area, and ConUI draws it there.

```
      ┌── OS frame: square corners, OS shadow ───────────────────────┐
      │ ▫              CONUI · PLAYGROUND              ▭ ▫ ⊿        │ ← row 0: caption
      │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀│ ← bottom of the band (bg.raised), no rule
      │                                                              │ ← row 1: empty
      │ FILE  VIEW  WIDGETS  [THEME ▾]  HELP        PHOSPHOR · 88%  │ ← row 2: menu bar
      │──────────────────────────────────────────────────────────────│
      │                                                              │
      │  window body: panels, splitters                              │
      │                                                              │
      │──────────────────────────────────────────────────────────────│
      │ READY      F1 HELP   F11 FULLSCREEN        124×41  2 draw ◆  │
      └──────────────────────────────────────────────────────────────┘
```

**The window has no frame of its own.** The OS draws the border, and a
second line next to it would be two frames in a row. Panels inside keep
their frames, so `┌─[ CONTROLS ]─┐` is the only frame on screen that ConUI
draws.

#### Caption

Classic Mac, but flat: no bevel, no gradient, no shadow, every line one pixel.

- **No System 7 stripes.** In a dense grid, six lines a pixel apart make the
  caption the noisiest element on screen and fight the panel frames. What
  remains is what the Macintosh caption is worth borrowing for: the flat
  band, square boxes and a centred title.
- **The title is centred**, bold, `text.primary`.
- **The close box is on the left**: an empty square, a one-pixel outline in
  `danger`. In System 7 it shared the colour of the other boxes, but closing
  a window is irreversible, and red is reserved for that (§5).
- **Three squares on the right**: collapse (a line across), zoom (a small
  square in the corner), fullscreen (two opposite corner marks). Fullscreen
  is also `F11`.
- **The boxes are raster icons from `resources.dat`** (§16). One sheet per
  pixel size: `caption_<N>.png`, N = 8 to 32, a strip of five N × N squares:
  close, collapse, zoom, fullscreen, filled (pressed). They are **white masks
  on transparency**; when drawn, the sprite is tinted with a theme slot
  (`danger` for close, `accent` under the pointer, `border` or `text.muted`
  otherwise), so the same files serve every theme and every state. The box
  size is `round(0.55 · cell_h)` (8, 9, 11, 13 px at 100 % and up to 26 px at
  200 %); the sheet of exactly that size is drawn at its native size on
  whole pixels, without scaling, so one-pixel lines stay crisp. Without an
  exact sheet, the nearest one is used, also at native size; without any
  resources, the boxes are drawn as geometry. `tools/capicons.cpp` generates
  the sheets; they can be replaced with any art of the same layout.
- **Focus, the Macintosh way: an inactive window loses its boxes.** An empty
  band and a muted title (`text.muted`) remain. No second frame colour is
  needed for this.
- **No rule under the caption.** The `bg.raised` band separates itself from
  the window background, and the menu bar below has its own rule.
- **The boxes are buttons**: the pointer turns into a hand over them, as over
  every clickable element (§9). The window loop (`main.cpp`) decides this
  from `AppWindow::hoveredButton()`, because the caption is not drawn through
  the UI context.
- **The band is a third taller than a row** (`round(4/3 · cell_h)`, 27 px at
  Normal). Text and boxes are centred in it with equal space above and
  below. In whole cells the band still occupies row 0; the extra third spills
  into row 1, which is always empty. So the band is a geometry shape, not a
  surface (surfaces come only in whole cells), and the renderer lowers the
  glyphs of row 0 by half the extra (`chrome::CaptionTextPadPx`). The pixel
  height is rounded once (`chrome::CaptionHeightPx`), and both the band and
  the OS drag zone are computed from it.
- **The band reaches the right edge of the window**, not the last whole
  cell: the remainder of the width is painted too.
- **Row 1 is empty; content starts at row 2.** The caption is not part of
  the content, and the menu bar must not stick to it. The theme editor
  (§8.16) follows the same rule.
- **Dragging** is by the band; the OS is told about it
  (`WM_NCHITTEST` → `HTCAPTION`, `isMovableByWindowBackground`) and the
  window manager moves the window. A double click maximises. The columns
  under the boxes are cut out of the drag zone. Both the band and the cut-outs
  are expressed **in client pixels**: a maximised window's client area is
  offset by the invisible resize border.
- **Hovering a box is tested against the box itself** (plus 2 px of slack on
  every side), not against the band around it.
- **Fullscreen has no caption.** It is not drawn, its boxes do not react, the
  band does not drag the window, and the application receives
  `AppServices::fullscreen` and starts its content at row 0 instead of 2.
  The window edges cannot be dragged in fullscreen either.
- **The bottom of a fullscreen window has exactly half a cell of space**
  under the status bar, so it does not sit on the very edge of the monitor.
  The remainder of the height goes to the **top** (`AppWindow::gridOffsetY`).
  Row 0 glyphs are not lowered in fullscreen: that row is the menu bar, not
  a caption.
- **Fullscreen from a maximised window** covers the taskbar too. Entering
  saves the whole window placement (`WINDOWPLACEMENT`, including the
  maximised flag), un-maximises and stretches the window over the monitor;
  leaving restores the placement exactly.

#### Frame and platforms

- **Windows 11.** The window styles stay (`WS_THICKFRAME | WS_CAPTION |
  WS_MINIMIZEBOX | WS_MAXIMIZEBOX`): Snap, `Win`+arrows, the shadow and the
  minimise animations depend on them. `WM_NCCALCSIZE` removes the caption but
  **keeps the frame**. `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_DONOTROUND`
  gives square corners.
- **macOS.** `FullSizeContentView` + `titlebarAppearsTransparent` +
  `titleVisibility = hidden`, the three `standardWindowButton:` hidden.
  NSWindow always rounds its corners; that is the visible difference from
  Windows. The macOS path is written but has not been run on real hardware.
- **Linux and other systems.** GLFW only (`Platform_other.cpp`). There is
  no portable way to fold the title bar into the client area, so the window
  keeps the desktop's own title bar, and the ConUI caption row is drawn
  under it. Fullscreen and the work-area clamp use plain GLFW calls. Written
  but not yet run.
- **The OS frame colour is not tinted to the theme.** It belongs to the OS.
- **Resizing** is by the edges, through the OS. The step is one pixel; the
  grid is recomputed and the remainder filled with background. The window
  redraws **while** an edge is being dragged: on Windows that happens in a
  nested modal loop that blocks the main one, so a frame is also produced
  from the GLFW refresh callback, by the same function as in the main loop.
- The window is **not** `GLFW_DECORATED = false`: the styles stay native and
  only the caption is hidden. Otherwise Snap and the shadow would be lost.
- The bottom row is the status bar: state on the left, metrics on the right.
  It is the **very last** row of the grid, with a full-width rule above it
  and nothing below.
- The rule above the status bar is **drawn half a row above its own row**
  (`DrawRule(…, raised)`): about 10 px from the bottom frames of the
  sections, the same as the seam line between stacked panels (§8.17). The
  cells do not move; the sidebar reaches the middle of the raised rule.

### 8.2 Button

```
normal        [ RUN ]        hover         ▐ RUN ▌     (inversion)
pressed       ▓ RUN ▓        disabled      [ RUN ]     (text.muted)
default       ◆ RUN ◆        danger        [ DELETE ]  (danger)
```

One cell high. There are no other button variants: a "3D" button with a
frame and a `▒` shadow would drop by a cell when pressed and jump visibly.
The primary action of a screen is marked with the default variant
(`BTN_DEFAULT`). Like every clickable element, a button turns the pointer
into a hand (§9).

### 8.3 Progress bar

**The fill is smooth, not blocky.** The bar is a rectangle in the geometry
layer (§2.1), 0.62 of a cell high with a 0.14 corner radius: a `dither`
track with an `accent` fill whose width is the exact fraction, not a rounded
one.

```
label on the left, value on the right, a fill without steps:

  RESOURCES  ▐▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬░░░░░░░░░░▌   64%   ← a solid bar, the edge where it belongs
  TESTS      [■■■■■■■■■■■■■■□□□□□□□□]  14/22      ← the retro variant
  PENDING    ▐░░░░▬▬▬▬▬▬░░░░░░░░░░░░░░░░░▌        ← the pendulum highlight: back and forth
```

The diagram is an ASCII approximation; the real render is a smooth bar.

Why not eighth blocks: at 15 cells wide one eighth is 0.83 % of the scale,
and a bar updated every frame visibly jerks. A smooth fill costs one
rectangle in the geometry layer and removes the problem.

The indeterminate indicator (`ProgressBusy`) is the same track with a
highlight instead of a fill. The highlight travels **back and forth** inside
the track (1.6 s per pass), slowing at the ends; its soft tail always trails
behind the direction of travel. It is one of the system's animations (§9).

**The retro variant `[■■■■□□□□]`** (`ProgressRetro`) is entirely glyphs, for
places that want a terminal texture (a build log, command output). Filled
ticks are `■` in `retro.filled`, the rest `□` in `retro.empty`, so the whole
length of the scale is always visible. Both slots can be edited in the theme
editor, and its live preview includes a retro bar.

### 8.4 Slider

A line and a square knob:

```
  OPACITY     ├────────────────■──────────────┤   85%
  stepped     ├──┼──┼──┼──■──┼──┼──┼──┤            5/8
  range       ├──────■──────────■─────────┤        12 … 48
```

Drag the knob, use the wheel, `←`/`→`, `Shift` for a fine step. In a range,
the span between the knobs is drawn in `accent`, the rest of the line in
`border`: colour again, instead of line weight.

**The whole slider is geometry.** The diagram above draws it with glyphs for
clarity. The line, the end caps, the step ticks and the knob are shapes, and
they sit at the **exact fraction** of the track length, not on whole cells.
One formula (`posOf`) places the ticks and the knob and maps the pointer
back to a value, so the knob always sits on its tick at any width.

- **The line** runs from the centre of the first cell to the centre of the
  last, as thick as the font's stroke (`BoxLineIn`: 100 of the 600 units of
  the cell width), at the height of `─`. The part up to the knob (between
  the knobs, for a range) is drawn over it in `accent`.
- **End caps** are 60 % of the row height. The glyphs `├ ┤` are taller than
  the cell and, in sliders on neighbouring rows, would merge into one
  vertical.
- **Step ticks** are shorter, 36 % of the row, and drawn only when at least
  two cells apart; inside the `accent` span they are `accent` too.
- **The knob** is a square the size of the font's `■`, centred on its value.
- While the focus `◆` blinks at the left end (the slider's only glyph), the
  left end cap is not drawn.

### 8.5 Checkbox and switch

```
checkbox  [ ] Off          [✓] On           [~] Mixed        [ ] Disabled
radio     ( ) Option A     (●) Option B
switch    ░░▓▓ ON           ▓▓░░ OFF        (shapes, like the progress bar)
```

**The switch looks like the progress bar**: a track three cells long of the
same height (`BAR_H`), with the same rounded corners (`BAR_R`) and the same
colour (`dither`); the thumb is half the track. When `ON`, it sits on the
right in `success`, and the `ON` label is `success` too; when `OFF`, it sits
on the left, muted (`text.muted`). Under the pointer and in focus, an off
thumb takes the widget's edge colour; pressed, `text.primary`. Focus is a
blinking 1 px `border.focus` frame around the track (a `◆` glyph would sit
on top of the shape).

### 8.6 Dropdown

```
closed:   DENSITY  ▌ Normal                  ▼▌
open:     DENSITY  ▌ Normal                  ▲▌
                 ┌────────────────────────────┐
                 │ Tiny                       │
                 │ Compact                    │
                 │▐ Normal                  ✓▌│  ← inversion and a check mark
                 │ Large                      │
                 └────────────────────────────┘
```

The field is a full-width `bg.sunken` strip with `▼` at its right end. The
list opens below the field, or above it when there is no room.

### 8.7 Directory tree

```
┌─[ FILESYSTEM ]──────────────────────────┐
│ ▼ conui                                 │
│   ├─▼ docs                              │
│   │   ├── README.md               4.1K  │
│   │   └── DESIGN_SYSTEM.md        9.7K  │
│   ├─▶ tools                   (3)       │
│   ├─▼ src                               │
│   │   ├─▶ Core                          │
│   │   └─▐ UI                      ▌     │  ← full-width selection
│   └── CMakeLists.txt              6.8K  │
└─────────────────────────────────────────┘
```

Connectors `├── └── │`, folding with `▼`/`▶`, a selection inverted across
the full width of the row. A collapsed folder shows its child count.

**The content of every section starts right under its title**, the same for
all of them: a plain panel, `PANEL_FLUSH` (the tree), the table, the
editable table, the multi-line field and the charts. The space above the
content comes from the frame itself: the panel ground starts at the frame
line, and the frame of a lower panel under a seam is raised as well (§8.17).
The exception is the tabbed panel: the bottoms of the inactive tabs hang in
the row under the frame (§8.15). `PANEL_FLUSH` only drops the side margins,
for full-width selection.

### 8.8 Table

```
┌─[ PROCESSES ]───────────────────────────────── 124 rows ─┐
│ PID    │ NAME              │ CPU  ▼│ MEM      │ STATE    │
│  1042  │ conui.exe         │ 12.4% │  38.2 MB │ RUNNING  │
│▐ 2288  │ build.exe         │  8.1% │ 112.0 MB │ RUNNING ▌│  ← selection
│  3310  │ ninja.exe         │  0.0% │   4.8 MB │ IDLE     │
│  ····  │ ·                 │  ···  │      ··· │ ·        │
└──────────────────────────────────────────────────────────┘
```

The header is `bg.raised`, the `│` separators are `table.colsep`, the sort
arrow `▼`/`▲` sits in the header, and the row count sits in the top frame on
the right. **There is no rule under the header**: the rows start right under
it, the header stands out by its ground alone, and the column separators run
from the header into the rows without `┼` joints. Numeric **data** is
right-aligned, but **column titles are always left-aligned**, and the sort
arrow sits at the right edge of its column whatever the alignment of the
data. A header click turns the pointer into a hand.

The horizontal scrollbar sits in the last row before the bottom frame and
rests against it (§8.14).

**Without a frame: `TBL_BARE`**:

```
 PID    │ NAME              │ CPU  ▼│ MEM      │ STATE   ▲   ← the header is the top row
  1042  │ conui.exe         │ 12.4% │  38.2 MB │ RUNNING █
▐ 2288  │ build.exe         │  8.1% │ 112.0 MB │ RUNNING▌█
  3310  │ ninja.exe         │  0.0% │   4.8 MB │ IDLE     ▼
```

No frame, no title, no row count: the table fills the whole rectangle, the
scrollbar in its last column. With no frame to keep it off a seam below,
**the bottom row of the rectangle is the table's own margin**. When the
columns overflow, the horizontal scrollbar sits in that same row, **centred
in it**, not in a row of its own: the scrollbar zone is one row and no data
row is lost (`ScrollbarH(…, centred)`).

### 8.9 Modal

```
        ┌─[ CONFIRM ]──────────────────────────────┐
        │                                          │
        │  Delete 3 selected items?                │
        │  This action cannot be undone.           │
        │                                          │
        │        ▐ DELETE ▌      [ CANCEL ]        │
        └──────────────────────────────────────────┘
```

The frame is **single**, like everything else: the line weight is the same
everywhere. No shadow. A modal stands out by its frame colour (`danger` for
`MODAL_DANGER`) and by dimming the window beneath it: **a solid translucent
rectangle** in `shadow` (the alpha comes from the slot) over the whole
window. Text under it is also dimmed (`ATTR_DIM`), so it can be guessed at
but does not compete.

**The modal background follows the same rules as a section**: its own
`bg.panel` surface cut to the frame line, and a ground patch under the
`[ … ]` title cut to the brackets (§8.17). This works because of the layer
order (§2): the dimming is its own layer between the finished window and the
overlay band, so the modal itself is never dimmed, and the dimmed window
shows around it right up to the frame line. The dimming also covers whatever
is drawn **after** the modal in the same frame: the whole overlay band comes
after the content band.

While a modal is open, the rest of the window takes no input and `Tab`
cycles inside the modal only. The default focus goes to the `BTN_DEFAULT`
button; in a destructive confirmation that is CANCEL, so an `Enter` pressed
out of habit deletes nothing. `Esc` closes it.


### 8.10 Context menu

```
┌──────────────────────────────┐
│ Open                   Enter │
│▐Rename…                  F2 ▌│  ← inversion under the pointer
│ Copy path             Ctrl+C │
├──────────────────────────────┤
│ ✓ Show hidden                │
│ ▸ Sort by                    │  ← submenu
├──────────────────────────────┤
│ Delete                   Del │  ← danger
└──────────────────────────────┘
```

A right press inside the area opens the menu at the pointer. A chosen item
closes the whole chain, `Esc` closes the innermost menu, a click outside
closes all of it. The same menus open from a menu bar (`MenuBarItem`):
while one bar menu is open, hovering another title switches to its menu.

### 8.11 Tooltip

```
        ┌────────────────────────────────┐
        │ Window background opacity.     │
        │ Shift: fine step.              │
        └────────────────────────────────┘
```

Appears after about 450 ms, sticks to the pointer, flips at the window
edges. No shadow, like menus, dropdown lists and modals (§4).

### 8.12 Single-line input

```
  NAME   ▌conui_playground█                     ▌   ← block cursor
  SEARCH ▌▒search by name…                      ▌   ← placeholder
  PORT   ▌80800                               ✗ ▌   ← error (danger)
  PATH   ▌C:\projects▐conui▌                    ▌   ← text selection
```

The field is a `bg.sunken` strip. The cursor is a blinking `█`, `▁` in
overwrite mode. Editing is by codepoint (one codepoint is one cell):
typing, Backspace and Delete (with `Ctrl`, by word), `←`/`→` (with `Ctrl`, by
word; with `Shift`, select), `Home`/`End`, `Insert` (overwrite),
`Ctrl+A/C/X/V`, click, drag selection, double click selects a word. An
optional history list (the caller's own) is walked with `↑`/`↓`.

### 8.13 Multi-line input

```
┌─[ NOTES ]──────────────────────────────────────────┐
│ 1  First line of the document.                     │
│ 2  Second line, cursor is here█                   ▮│
│ 3                                                 ▮│
│ 4  ~                                               │
│    ~                                               │
├────────────────────────────────────────────────────┤
│ LN 2, COL 26           UTF-8   LF         4 lines  │
└────────────────────────────────────────────────────┘
```

Line numbers in a gutter, `~` for rows past the end of the text (as in vi),
its own status bar at the bottom. The gutter is one cell of margin from the
frame (as in every panel), exactly as many digits as the last line number
needs, and two empty cells before the text; numbers are right-aligned.
Everything the single-line field does, plus `Enter`, `↑`/`↓` (keeping the
column), `PgUp`/`PgDn`, `Ctrl+Home/End`. The wheel scrolls the view without
moving the cursor. `Tab` still moves the focus: a text area that swallowed
`Tab` would be a keyboard trap.

### 8.14 Scrollbars

**Narrow, with rounded ends, no arrows, no visible track.** The only widget
that lives entirely in the geometry layer (§2.1).

```
        width 0.55 of a cell (about 5 px), round ends, no arrows

  panel  │                                     │ panel
         │  content                         ▮  │   ▮ = thumb
         │                                  ▮  │       length ∝ visible fraction,
         │                                  ▮  │       at least 1.2 cells
         │                                     │
         └─────────────────────────────────────┘
                     ▬▬▬▬▬▬▬▬▬▬            ← horizontal: the same thickness
```

- **No arrows at the ends.** They would take two cells of length and need
  auto-repeat and a 9 × 20 px target. The wheel, dragging, `PageUp`/`PageDown`
  and a click on the track cover the same ground.
- **No track is drawn.** While there is nothing to scroll there is no thumb,
  and the panel carries no extra vertical line.
- **The thumb belongs to its region, not to the window**: it shows only while
  the region is under the pointer or holds the focus, so a panel at rest is
  clean. A drag that has started keeps the thumb even if the pointer leaves
  the region; a region under an open overlay does not count as hovered. The
  widget learns this through the `live` parameter; `ScrollLive()` answers it
  for a rectangle.
- The thumb takes **one layout cell**, but is drawn half as wide, inset 0.3 of
  a cell from the ends of its run.
- Colour: `accent.dim`, `accent` under the pointer; the thickness never
  changes. **The thumb is always opaque.** The pointer becomes a hand over it.
- Positioning is subpixel: the thumb sits where it belongs, not in the
  nearest cell.
- **The horizontal one** is the same object turned sideways, the same
  thickness **in pixels**: at 9 × 20, 0.55 of a column is 0.25 of a row. It
  serves tables whose columns are wider than the panel. It **rests against
  the bottom of its row**, that is, against the bottom frame, not centred in
  the row (except in a bare table, §8.8).
- A `-- more --` line remains an option for lists where a scrollbar is
  overkill.

### 8.15 Tabs

The tab "breaks out" of the frame:

```
┌─[ WIDGETS ]──┐ DATA ├─┐ INPUT ├───────────────────────┐
│              └──────┘ └───────┘                       │
│  content of the active tab                            │
```

The label of an inactive tab rises above the frame line, where the panel
ground has already ended (§8.17), so a ground patch lies under it: from the
top of the cells to the line, as under a panel title, and across from the
outer edge of the tab's left vertical `┐` to the outer edge of its right
`├` (`EDGE_L`/`EDGE_R`), not just under the letters; the corners are square.
The bottom edge of an inactive tab `└──┘` is drawn a quarter of a row above
its own row (about 5 px at Normal, `RAISE_QUARTER`), closer under the label;
the cells do not move. The active tab is a `[ … ]` title, its patch cut to
the brackets. Click or `←`/`→` on the focused strip switches tabs.

### 8.16 Theme editor

**A real second OS window**, not a panel inside the main one: its own GLFW
window, its own `CellBuffer`, its own OS frame and its own System 7 caption.
It can be moved to another monitor. The GL context and the glyph atlas are
shared with the main window. There are no internal floating windows (MDI);
see §13.

The window grid is 60 × 34 cells. Two columns, divided by a draggable seam:
on the left, the list of all 32 palette slots; on the right, the editor of
the selected slot and a live preview.

```
 ▫                   THEME EDITOR                    ▭ ▫ ⊿

  [ NEW ] ◆ SAVE ◆ [ REVERT ]
 ───────────────────────────────────────────────────────────────
  THEME  ▌ Phosphor                            ▼▌
  FILE   themes/phosphor.json                         ✓ saved
 ───────────────────────────────────────────────────────────────
  SLOT              HEX      ▮ ┊ [ bg.panel ]
  ██ bg.window   #062038     ▮ ┊
 ▐██ bg.panel    #0B3055 ▌   ▮ ┊   ██████████   #0B3055
  ██ bg.raised   #124070       ┊
  ██ bg.sunken   #03182A       ┊  R ├■──────────────┤        11
  ██ bg.selection#1FA35C       ┊  G ├───■───────────┤        48
  ██ border.dim  #1B5585       ┊  B ├──────■────────┤        85
  …                            ┊  A ├──────────────■┤       255
                               ┊
                               ┊  HEX ▌#0B3055█             ▌
                               ┊
                               ┊  ─ LIVE PREVIEW ──────────────
                               ┊  ┌─[ PANEL ]─────────────────┐
                               ┊  │ text.primary              │
                               ┊  │▐ selected row            ▌│
                               ┊  │ [ BTN ] ▬▬▬▬▬▬░░░░    62% │
                               ┊  │ [■■■■■□□□]           5/8  │
                               ┊  │ ░▒▓ dither   ✗ danger     │
                               ┊  └───────────────────────────┘
 ───────────────────────────────────────────────────────────────
  32 SLOTS  3 changed                   Ctrl+S save · Esc close

  ▮ = the list's scrollbar (§8.14)   ┊ = the splitter between the columns (§8.17)
```

Details:

- **A slot swatch paints itself.** The `██` cell in a list row is drawn with
  `fg` set to the slot being shown. No separate "show a colour" mechanism is
  needed: it is just a glyph in that colour.
- **Edits apply live**, to the preview on the right and to the main window.
  While the theme is unsaved, the header shows `● modified` and `REVERT` goes
  back to the file.
- **The R/G/B/A sliders** are the ordinary `Slider` from §8.4; nothing new.
  The `A` channel matters only for slots with transparency (`shadow` and the
  backgrounds).
- **The HEX field** is the ordinary `TextInput` from §8.12 with validation: a
  wrong string turns `danger` and is not applied, rather than resetting the
  colour.
- **Window and section opacity** are not part of the theme but of the
  application settings (§6): they belong to the window, not the palette.

The user guide is [THEME_EDITOR.md](THEME_EDITOR.md).

### 8.17 Splitter

Seams between panels can be dragged. A window layout is a **tree of splits**
(vertical and horizontal), not a set of independently floating panels: the
border between two neighbours moves, they give and take from each other, the
sum stays the same, and no gap can open up.

```
  at rest                  under the pointer / while dragging

  ┌────────┐│┌────────┐    ┌────────┐┃┌────────┐
  │ PANEL  │││ PANEL  │    │ PANEL  │┃│ PANEL  │   ┃ = a 2 px line, accent
  │        │││        │    │        │┃│        │
  │        │││        │    │        │┃│        │
  └────────┘│└────────┘    └────────┘┃└────────┘
            └ a border.dim hairline
```

- **A panel background never leaves its frame**: the frame line runs through
  the middle of the edge cells, and the ground ends at the outer edge of that
  line, not at the edge of the cell (a surface with the `framed` flag, §2).
  The `[ TITLE ]` sits on the top line and reaches half above it, so a ground
  patch lies under it, from the top of the cells to the top edge of the line,
  exactly where the panel ground begins, without overlap (`EDGE_CAP`). The
  patch never goes past the outline of the `[` and `]` brackets; the renderer
  takes their edges from the atlas (`EDGE_INK_L`/`EDGE_INK_R`). The same goes
  for inactive tab labels (§8.15). A table's row count and a chart's legend
  are not titles and get no patch.
- **The same for popups**: tooltips, menus (context and menu bar) and
  dropdown lists. Each has its own `bg.raised` ground, a surface in the
  overlay band cut to the frame line. Cells inside whose background equals
  the frame's ground do not paint their own cell (the surface does); other
  backgrounds (a selected item) are painted as usual. `DrawFrame` always lays
  down such a surface.
- **Between side-by-side neighbours: a one-column seam**, and the splitter
  lives in it. Between the frame lines of the two panels: two column widths
  (about 18 px at Normal).
- **Between stacked neighbours: a one-row seam**, empty: it is window
  background, and panel grounds do not enter it. The vertical gap is larger
  than the horizontal one (about 40 px against 18 px between frame lines),
  but the lower panel is clearly separated from the upper one, including a
  frameless table (§8.8), and the seam has room for the splitter line.
- At rest: a `border.dim` hairline, opaque. In a vertical seam it runs down
  the middle, 0.12 of a cell wide; in a horizontal one it has the same pixel
  height and runs along the **top edge** of the seam row, half a row under
  the upper panel's bottom frame. The whole row is the grip.
- **The top frame of the lower panel is raised towards the seam line**,
  halving the space between them. Only the drawing of that frame row moves
  (the line, the `[ ]` title with its patch, the top edge of the panel ground),
  and the frame's verticals stretch up to the raised line. The cells do not
  move: content, the bottom frame and mouse hits stay where they are; the
  first row under the title simply gets more air. `Split` marks the row
  (`CellBuffer::raiseRow`), and the renderer shifts it by
  `(cell_h + y0 - seam line) / 2` pixels (about 14 px at Normal). Note: tabs
  in such a row move visually while their click zone does not.
- Under the pointer and while dragging: a line **exactly 2 px** wide in
  `accent`, opaque, on whole pixels where the resting line is; the pointer
  becomes `↔` or `↕`. The thickness is in pixels, not cell fractions, so it is
  the same at every density: the window reports the cell size in pixels
  (`ui::Input::cell_w/cell_h`).
- The step is **one pixel**, but panels stay whole cells: the seam position
  rounds to the nearest cell boundary, so content never sits between two
  columns.
- Each panel has a minimum size in cells; the splitter stops at it.
- The layout is saved in `data.bin`. A double click on a seam returns it to
  its default fraction.

### 8.18 Sidebar

A vertical column of section icons at the left edge of the window.

```
  FILE  VIEW  WIDGETS  THEME  HELP
 ─────────────────────────────────────────  ← rule under the menu bar
 ▓▓▓▓ ┌─[ WIDGETS ]──────
 ▓■▓▓ │                    ■ = the active section: sidebar.active
 ▓▓▓▓ │
 ▓▣▓▓ │                    ▣ = the others: sidebar.icon,
 ▓▓▓▓ │                        sidebar.hover under the pointer
 ▓▦▓▓ │
 ▓▓▓▓ └──────────────────
 ─────────────────────────────────────────  ← rule above the status bar
  READY  F1 help …
```

- **Height**: from the rule under the menu bar to the rule above the status
  bar; without them, the full height of the window. The application passes a
  rectangle in whole rows, and the sidebar ground reaches into the rows of
  the rules **as far as the middle of their stroke** (`bleedTop = 1 -
  RULE_MID`, `bleedBottom = RULE_MID`, `RULE_MID = 0.49`, where the font draws
  `─`), on whole pixels. The edge of the ground hides under the stroke, and
  the sidebar runs exactly from rule to rule.
- **Width: 4 columns** (`SIDEBAR_W`, 36 px at Normal). Its own `sidebar.bg`
  ground, **no edge line**: only colour separates the strip from the content,
  as with the caption band.
- **A section is a two-row slot** with an icon **one row high** (20 px at
  Normal) centred in it, on whole pixels. Slots that do not fit are dropped
  whole (there is no scrolling).
- **The pointer answers to the icon, not to the slot around it**: the hover
  zone is the icon's rectangle plus 2 px, although the slot is four times
  larger. Clipping and focus still follow the whole cells of the slot.
- **Selection is shown by icon colour only**, without fills or bars: the
  active icon is `sidebar.active`, the one under the pointer `sidebar.hover`,
  the rest `sidebar.icon`. While the sidebar holds the keyboard focus, the
  active icon is `border.focus`, as focus is everywhere in the system.
- **All sidebar colours are theme slots** (§5): the ground and the three icon
  states can be edited in the theme editor and show in the main window at
  once.
- **Every icon has a tooltip** with the section name (§8.11), and the pointer
  turns into a hand.
- **Keyboard:** one `Tab` stop; while focused, `↑`/`↓`/`Home`/`End` switch the
  section. Clicking an icon gives the sidebar the focus.
- **Icons are raster masks from `resources.dat`**, like the caption boxes
  (§16): a family `<prefix>_<N>.png`, one sheet per pixel size, tinted with a
  theme slot, drawn at native size without scaling. `LoadIconStrip` loads
  them, the same loader as the caption's. Without an icon, a `■` is drawn in
  the slot.

**In the playground** the sidebar switches the same five screens as the VIEW
menu (Workspace, States, Glyphs, Charts, Diagrams). The icons are
`sections_<N>.png`, N = 12 to 48 (from Tiny at 100 % to Large at 200 %),
drawn by `tools/secicons.cpp`: a grid of panels, a list of states, a glyph
table, axes with bars, a tree of three boxes.

### 8.19 Editable table

```
┌─[ TASKS ]──────────────────────────────────────── 5 rows ─┐
│ TASK ▲          │ OWNER    │ NOTES                        │
│ Editable table  │ ceo      │ Enter / F2 edits             │  ← a row grows
│                 │          │ Shift+Enter: new line        │    to its tallest cell
│ ▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔ │  ← 1 px table.rowsep
│ Glitches        │ anna     │ Tear, jolt, noise            │
│                 │ █        │ Off in SETTINGS              │  ← the current cell
└───────────────────────────────────────────────────────────┘
```

`ui::EditTable` (`Widgets/EditTable.cpp`). Geometry, header, sorting and the
row count are the same as the table's (§8.8). The differences:

- **The selection is a cell, not a row.** A focused cell is `bg.selection`
  with `text.inverse`; without focus, `bg.raised`. The cell being edited is
  `bg.sunken` with the cursor and selection of the shared editing core
  (§8.12, §8.13).
- **A row grows with its text.** Its height is the line count of its tallest
  cell, at most 6; text that does not fit ends its last visible line with
  `…`. Rows are divided by a 1 px `table.rowsep` line (geometry, §2.1), so a
  multi-line row reads as one unit. Cells do not wrap: a long line is cut with
  `…`, and in edit mode it scrolls with the cursor.
- **Spreadsheet keys.** Not editing: arrows, `Home`/`End` (with `Ctrl`, the
  first and last row), `PgUp`/`PgDn`. `Enter`, `F2` or a double click start
  editing and keep the cell's text; typing a character starts editing and
  replaces it; `Delete` clears the cell. Editing: `Enter` commits,
  `Shift+Enter` inserts a line break, `Esc` cancels, `Tab`/`Shift+Tab` commit
  and move to the next or previous cell (wrapping to the next row). Leaving
  the table commits too. Everything else (selection, clipboard, `↑`/`↓`
  between lines) works as in the multi-line field.
- **Rows are inserted and deleted** with `Ctrl+Insert` / `Ctrl+Delete` or from
  the row's context menu: "Insert row" and "Delete row" (`MENU_DANGER`).

The data and the row order belong to the caller. The widget edits cells in
place through a `std::string` pointer (`nullptr` makes a cell read-only),
and reports everything else as events: `ET_SORT`, `ET_INSERT` and
`ET_DELETE`, with the row in `evRow`. The caller sorts, inserts and deletes.

### 8.20 Charts

```
┌─[ TRAFFIC ]───────────────────────────────── ■ rx ■ tx ─┐
│   75 ┤ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄╱╲┄ ┄ ┄ ┄ ┄ ┄ │  ← ticks on whole rows,
│      │                          ╱╲    ╱   ╲            │    a 1 px dashed grid
│   50 ┤ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ╱┄ ┄╲┄╱┄ ┄ ┄ ┄╲┄ ┄ ┄ ┄ ┄ │
│      │                 ╱                       ╲         │  ← lines are vector segments
│   25 ┤ ┄ ┄ ┄ ┄ ┄ ┄ ┄╱┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄ ┄╲┄ ┄ ┄ │
│      │ ‾‾‾‾‾‾‾‾‾‾╱                                   ╲   │
│    0 └────┬────┬────┬────┬────┬────┬────┬────┬────┬────  │  ← the axis is glyphs
│          00   02   04   06   08   10   12   14   16      │
└──────────────────────────────────────────────────────────┘
```

`ui::LineChart` and `ui::BarChart` (`Widgets/Chart.cpp`). Everything that
reads as text (the frame, title, legend, axes, labels) is glyphs on the grid.
The data itself is the geometry layer (§2.1): lines are segments, bars are
rectangles with the progress bar's rounding, the grid is a 1 px dash.

- **A section frame** with a title, and a **legend** (`■ name` per series in
  `chart.N`) in the top frame on the right, where a table has its row count.
  A legend that does not fit is not drawn. Up to 4 series (`chart.1` to
  `chart.4`).
- **The value axis**: labels right-aligned up to `┤`, ticks on **whole
  rows** at least every 3 rows, so a label, its `┤` and its grid line always
  share a row. The range uses "nice" steps of 1, 2, 2.5 and 5 × 10ⁿ fitted to
  the data, or an explicit `vmin`..`vmax`. Thousands print as `k`.
- **The category axis**: `└───────` with labels under the ticks; labels that
  would collide are thinned out (every k-th). **The ticks of this axis (and of
  the value axis of horizontal bars) are geometry**, a short stroke down from
  the line as thick as the font's stroke, at the exact position of the point
  or value. The hover guide and dots use the same coordinate, so they always
  meet the tick. Labels stay text, centred on the nearest cell.
- **Lines** are `cell_h / 10` thick (2 px at Normal) with round caps.
  `CHART_SMOOTH` draws a smooth curve: a monotone cubic spline
  (Fritsch-Carlson) that **never overshoots the data**, so smoothing never
  invents peaks. A non-finite value is a gap in the line.
- **Bars** are grouped: in each category one bar per series, across 70 % of
  the group width, rising from zero. Vertical by default; `CHART_HORIZONTAL`
  puts the categories down the left side, the values along the bottom axis,
  and the grid vertical.
- **Hover:** for lines, a vertical `text.muted` guide, a dot on each series
  and a tooltip "category / series: value" (§8.11); for bars, the group is
  highlighted with `bg.raised` and its category label turns `text.primary`.
- **Grid** (`CHART_GRID`, on by default): a `border.dim` dash, 3 px on and
  3 px off, along the ticks, under the data.

The data and its order belong to the caller: a series is a name, a pointer to
`n` values and a colour slot. In the playground: the **Charts** screen (the
VIEW menu and the fourth sidebar icon) with TRAFFIC (two lines), CPU LOAD
(live, smooth, 0 to 100 %, a new sample every 0.5 s), COMMITS (vertical bars
by month) and DISK USAGE (horizontal bars).

### 8.21 Diagrams

```
┌─[ EXPRESSION TREE ]─────────────────┐ ┌─[ NETWORK ]─────────────────────────┐
│                 (÷)                 │ │                                     │  ← a box: its label on
│               ╱     ╲               │ │   n ●━━━━━●───────●╲                │    whole cells in a
│            (+)       (×)            │ │       ╲  ╱ ╲     ╱  ╲               │    rounded outline
│           ╱   ╲     ╱   ╲           │ │   r ●━━━━━●━━━━━━━●━━━━● P          │    one row high
│        (−x)   (√) (2)   (a)         │ │       ╱  ╲ ╱     ╲  ╱               │  ← edges are segments:
│                                     │ │   R ●─────●───────●╱                │    colour is the sign
│  wheel: zoom · drag: move · …       │ │  wheel: zoom · drag: move · …       │    of a weight, width
└─────────────────────────────────────┘ └─────────────────────────────────────┘    its size
```

`ui::TreeDiagram` and `ui::NetDiagram` (`Widgets/Diagram.cpp`): pictures of
structure rather than values, such as a formula as a tree of boxes and a
layered neural network. They have no frame of their own: they draw inside a
rectangle, a panel's inside or part of it under the panel's own controls.

- **Tree.** Nodes are an array of `DiagramNode` (label, parent index, colour
  slot, `hidden`); a parent comes earlier in the array than its children, a
  node without such a parent is a root, and several roots stand side by
  side. Leaves take one place each from left to right, a parent sits over the
  middle of its children; levels go top down at most 3 rows apart, the lowest
  above the hint row. The label is bold text **on whole cells**, with a
  rounded outline in the node's slot over a `bg.raised` ground: geometry, as
  are the lines from the bottom middle of a parent to the top middle of a
  child (`border`, 1.5 px). `hidden` removes a box and its line but **keeps
  its place**, so a tree being assembled step by step does not jump about.
  Under the pointer the outline is heavier (2.5 px), and the function returns
  the node's index, so a `Tooltip` can be attached right after the call.
- **Network.** Layers are an array of `NetLayer` (node count, slot, labels);
  weights come as one matrix per pair of neighbouring layers, row-major,
  `n(k+1) × n(k)`. Layers are columns from left to right, nodes are circles
  spread evenly down the height. One line per weight: `chart.1` for a positive
  weight, `chart.2` for a negative one, 1 to 2.5 px thick and 15 to 100 %
  opaque according to its size relative to the largest weight between those
  two layers; below 8 % a line is not drawn (it would only blur the picture).
  Without weights (`nullptr`) every connection is the same thin `border.dim`
  line. Labels go to the left of the first layer and to the right of the last;
  room is kept for the longest.
- **The view is shared by both**, kept in the context under the widget's id,
  like a scroll offset: the wheel zooms about the pointer (1× to 16×, a 1.25
  step) and the point under the pointer stays under it; dragging moves the
  picture; a double click shows it whole again. Zoom **spreads the picture
  apart** while the letters keep their size: text on the grid cannot grow by
  25 %, and structure needs room more than it needs large letters. Network
  nodes grow with the square root of the zoom, so crowded middle layers come
  apart. New content (another formula, another network) calls
  `ResetDiagram(ctx, id)`.
- Input goes through `interact`: an overlay on top takes the wheel and the
  clicks, and the wheel is taken with `takeWheel`. The pointer over a diagram
  is the four-way arrow (`Cursor::Move`).
- `DIAG_HINT` (on by default) puts the hint "wheel: zoom · drag: move ·
  double-click: reset" on the last row, in `text.muted`.

In the playground: the **Diagrams** screen (the VIEW menu and the fifth
sidebar icon) with EXPRESSION TREE, the larger root of the quadratic equation
(−b + √(b² − 4ac)) / 2a, and NETWORK, 3 inputs, two hidden layers of 6 and
one output.

---

## 9. States and interaction

| State | How it shows |
|---|---|
| idle | normal colours |
| hover | `bg.raised`, or the brackets `[ ]` swap to `▐ ▌`. The pointer becomes a hand over everything clickable: buttons, tabs, scrollbars, sliders, checkboxes, radios, switches, tree rows, menu items and the menu bar, dropdowns, table column headers, sidebar icons. Text fields show the I-beam, splitter seams `↔`/`↕`, diagrams the four-way arrow (§8.21) |
| active / pressed | inversion and a `▓` fill |
| focused (keyboard) | the widget's edge glyphs in `border.focus` and a blinking `◆` in place of its first glyph |
| selected | inversion across the full row |
| disabled | `text.muted`, background unchanged, no input, out of the `Tab` order |
| error | `danger` on the frame or the text with `✗`, and a short glitch (below) |
| loading | the busy bar's travelling highlight (§8.3) |

The focus `◆` never needs a cell of its own, so focusing a widget never
shifts the layout.

**The error glitch** is the one cyberpunk liberty of the system. A reaction
to an event, not a standing effect: the moment an element **turns** into an
error, each of its rows shifts sideways by 1 or 2 cells (never 0) and about a
third of its glyphs turn into `danger` noise (shades and blocks `░▒▓█▀▄`
only), then everything settles. Three steps of 70 ms, timed by the clock, not
by frames, so it looks the same at 30 and 144 fps. Staying in error does not
repeat it. The element's background does not move; only the glyphs jolt, so
the field keeps its shape. **No** scanlines, glow or CRT distortion.

It is `ui::Glitch(ctx, id, rect, error)` (`UI/Widgets/Glitch.cpp`), called
**after** the widget is drawn; it corrupts cells the widget has already
drawn, so any widget can have it. The text field with `TXT_ERROR` uses it: in
the playground, the `PORT` field (type letters, or a number above 65535),
and in the theme editor, the `HEX` field with an invalid colour. **In a
showcase** (the States screen, `Context::forced()`) the element stays in error
permanently, so there the glitch **loops**: 0.21 s of glitch, 0.2 s of pause.

**Whole-window glitches** (`Context::glitchScreen`), two kinds:

- **Tear** (`Tear`): an application fault or critical error. 2 to 4
  horizontal bands of 1 to H/6 rows shift in different directions by 1 to 4
  cells, in three 70 ms steps. Everything shifts: content, overlays, and
  their shapes and sprites. Surfaces (panel grounds) span many rows and stay
  put, which only adds to the tear.
- **Jolt under a dangerous modal** (`UnderOverlay`): when a `MODAL_DANGER`
  modal opens, the window **under** it jolts the same way; the modal itself
  (the overlay grid) stays still.

Both apply in `Context::end()` to everything the frame drew, **one at a
time**: while one plays, a new one does not start; right after it ends
(0.21 s) another may. Never in a showcase. In the playground, the `FAULT`
button on the WIDGETS tab triggers a tear, and opening the `DELETE…` dialog
triggers a jolt.

**The switch.** Every glitch, element and window alike, turns off with one
setting, "Glitch effects" (the SETTINGS tab of the playground). Flicker and
jolts bother some people. The value is stored in `data.bin`
(`AppState::glitches`), and the core passes it to the context every frame
(`Context::setGlitches`).

**Magenta.** The other pinch of cyberpunk is the `danger` of the MIDNIGHT
theme, `#FF3DC8` (§5). In PHOSPHOR and PAPER `danger` stays red.

**Animations.** The cursor `█` blink in text fields (about 530 ms) and the
pendulum highlight of the indeterminate progress bar (back and forth, §8.3).
The glitch above is a third, one-off. Applications may add their own (the
playground's live chart).

**Frames are drawn on demand.** The loop waits for an input event, or for the
nearest animation deadline: everything that moves by itself says
`Context::animate(seconds until the next frame)` every frame. Shared
mechanisms say it on a widget's behalf: the blink (`blinkOn` asks for a frame
at the phase change), the tooltip hover timer, the pendulum highlight and
both glitches. An application only asks for what it moves itself, such as a
live chart. Smooth animations ask for 30 frames a second, discrete ones for
the moment they change. On top of that there is a rare "pulse" (0.25 s): an
animation that forgot to ask runs coarser but does not freeze, and the
autosave keeps ticking. At rest an empty application uses about 0.3 % of a
core, against about 14 % with constant redrawing. The frame counter in the
status bar therefore shows the redraw rate, not the maximum possible.

The input model is **immediate mode**, as in Dear ImGui, but self-contained:
`if (ui::Button(ctx, "OK")) { … }`. Full keyboard navigation is required:
`Tab`, arrows, `Enter`, `Esc`.

---

## 10. Runtime files

| File | What it is |
|---|---|
| `ConUI.exe` | the Windows launcher, next to the data files. It sets `CONUI_APP_ROOT` and starts `bin/ConUIApp.exe` |
| `bin/ConUIApp.exe` and its DLLs | the application. A separate `bin/` keeps its DLLs private: Windows resolves an executable's own DLLs from its directory before `main()` runs, so only a separate process can control that search |
| `resources.dat` | the packed resources: the font and every icon sheet from `src/assets/*/`, built by `packer` at build time |
| `themes/*.json` | theme files (§15) |
| `data.bin` | settings and state: theme, locale, opacity of the window and of sections, density, window geometry, split fractions, glitch switch, and the application's own state string. Written atomically through `data.bin.tmp`, every 5 s when something changed, and on exit |

`GetExeDirectory()` resolves the root from `CONUI_APP_ROOT`, so the
application finds its files whether it was started through the launcher or
directly. On Windows everything lives in one folder: the installation is
portable, and copying the folder copies the settings. Linux uses the same
one-folder layout without the launcher: `ConUIApp` sits next to
`resources.dat`, `themes/` and `data.bin` (`build_linux.sh` stages it so),
and finds that folder through `/proc/self/exe`. On macOS
`resources.dat` lives in the bundle's `Contents/Resources`, while
`data.bin` and `themes/` go to `~/Library/Application Support/ConUI/`,
because a signed bundle cannot be written to.

---

## 11. Project structure

```
conui/
├── CMakeLists.txt
├── build.sh · build_macos.sh · build_linux.sh
├── docs/
├── skill/                  the Claude Code skill (skill/README.md)
├── src/
│   ├── main.cpp            entry point, loop, input
│   ├── launcher.cpp        (Windows)
│   ├── packer.cpp          builds resources.dat
│   ├── Core/
│   │   ├── Utils.*         paths (resources and data), data.bin with atomic writes
│   │   ├── ResourceManager.*
│   │   ├── AppWindow.*     an OS window with its own CellBuffer (there can be several)
│   │   ├── Json.*          a minimal JSON parser and writer (§15)
│   │   ├── ThemeStore.*    themes/*.json: scan, load, save, live edit (§15)
│   │   ├── RasterStore.*   loading and the atlas of raster elements (§16)
│   │   ├── IconSheets.*    icon families <prefix>_<N>.png from resources.dat
│   │   ├── stb_truetype.h · stb_image.h
│   │   └── Platform.hpp    native frame, material, fullscreen (§8.1)
│   │       Platform_win.cpp · Platform_mac.mm · Platform_other.cpp
│   ├── Render/
│   │   ├── GLLoader.*      hand-loaded GL entry points (no GLEW)
│   │   ├── FontAtlas.*     text rasterisation, scale locked to the cell (§3)
│   │   ├── CellBuffer.*    cells, surfaces, geometry and sprites (§2)
│   │   └── Renderer.*      layers into one batch; SDF rounding in the shader (§2.1)
│   ├── UI/
│   │   ├── Theme.*         the palette: 32 slots (§5), built-in themes
│   │   ├── Glyphs.*        the glyph vocabulary (§4)
│   │   ├── Strings.*       localisation: Id, T() (§14)
│   │   ├── Context.*       immediate mode: ids, focus, hover, layout, input
│   │   ├── Layout.*        the split tree, splitters, saved fractions (§8.17)
│   │   ├── Widgets/        one file per element, shared Widgets.hpp:
│   │   │                   Panel · Label · Button · Toggle · Progress ·
│   │   │                   Slider · Scroll · TextEdit (editing core) ·
│   │   │                   TextInput · TextArea · Table · EditTable · Chart ·
│   │   │                   Diagram · Tree · Overlay (tooltip, dropdown, modal) ·
│   │   │                   Menu · Tabs · Sidebar · Glitch; TableParts.hpp is
│   │   │                   shared by Table and EditTable
│   │   └── WindowChrome.*  the System 7 caption, window rules (DrawRule) (§8.1)
│   └── App/
│       ├── App.hpp         the seam between core and application
│       ├── Playground.*    the showcase: Workspace · States · Glyphs · Charts · Diagrams
│       ├── AboutCat.*      an optional extra of the playground (CONUI_ABOUT_CAT), not part of the system
│       ├── ThemeEditor.*   the second OS window (§8.16), playground only
│       ├── Skeleton.cpp    the empty application template (FORK.md)
│       └── Scene.* · SelfTest.*   core test scenes and --selftest
├── tests/snapshots/        text baselines of the playground screens
├── themes/                 phosphor.json · paper.json · midnight.json · onyx.json
└── tools/
    ├── pngmask.hpp         PNG writing and mask strips, shared by the icon generators
    ├── capicons.cpp        caption icons → src/assets/caption/
    └── secicons.cpp        playground section icons → src/assets/sections/
```

`resources.dat` packs the font and every icon sheet from `src/assets/*/`:
`caption_<N>.png` (N = 8 to 32) for the caption boxes, `sections_<N>.png`
(N = 12 to 48) for the playground's sidebar. The packer tells files apart by
name only, so names must be unique across folders.

`themes/` is not source: the folder is created next to the executable and
copied into `dist/`. The repository holds reference copies of the themes.

---

## 12. Dependencies

| Component | Provided by | Why it is not an extra dependency |
|---|---|---|
| window, input, GL context | **GLFW 3** | a system package (MSYS2, Homebrew, Linux distributions); writing Win32 and Cocoa backends by hand would take months |
| OpenGL entry points | **own loader, about 120 lines** | no GLEW |
| font rasterisation | **`stb_truetype.h`** | a single public-domain header, vendored in `src/Core/` |
| raster elements | **`stb_image.h`** | the same kind of header; it loads application pictures, not UI |
| rounded rectangles | **own, about 80 lines** | a signed distance field plus coverage; no vector graphics library for a handful of shapes |
| transparency, fullscreen, native frame | OS APIs through `glfwGetWin32Window` / `glfwGetCocoaWindow` | |
| everything else | the C++20 standard library | |

Third-party libraries: **one** (GLFW), plus two vendored headers. Dear ImGui
is not used: the immediate-mode core is ConUI's own, built around the cell
grid.

---

## 13. Deliberately out of scope

- Raster **widgets**. No button, icon, arrow or frame of the system is a
  picture: they are all glyphs. The raster layer (§16) exists for application
  content, not for system decoration. **The exceptions are the caption boxes**
  (§8.1) **and the sidebar section icons** (§8.18): PNG masks from
  `resources.dat` that can be replaced with any art. Separately, the
  **application icon for the OS** (shortcut, `.exe`, taskbar, dock) is
  `src/icon.ico` through `resources.rc`; it is never drawn inside the window.
- Animations beyond those in §9. Gradients, glow, scanlines and CRT effects.
  **No shadows at all**, soft or hard.
- Choosing an OS backdrop material in the interface (§6). Rounding only where
  §2.1 needs it: the scrollbar thumb, the splitter grip, the progress bar.
  Frames, panels, buttons and windows are never rounded.
- An **internal** frame around the window: the OS draws the border (§8.1).
- Proportional fonts, a second size, a second family.
- Docking and **internal** floating windows (MDI). A second **real OS
  window** is supported, and the theme editor (§8.16) is one: the window
  manager owns it, it can go to another monitor, and it needs no Z order,
  dragging or resizing inside someone else's client area.
- A custom backdrop blur through screen capture.
- Wide (CJK) characters, combining marks and IME input: the grid is one
  codepoint per cell.

---

## 14. Localisation

The default interface language is **English**. Russian ships as the second
locale, so switching is a working demonstration rather than a promise.

The string layer comes before the widgets, because retrofitting it into
written widgets costs more:

```cpp
const char* T(Id id);            // the string in the current locale
// in code:  ui::Button(ctx, str::T(str::ID_RUN))  rather than  ui::Button(ctx, "RUN")
```

- `str::Id` is an enumeration (`src/UI/Strings.hpp`); the string table sits
  next to it in a plain array (`Strings.cpp`), one row per id: a key, a width
  budget in cells, and one column per locale. No gettext, no `.po`, no
  external formats.
- The locale is stored in `data.bin` and switches at runtime without a
  restart.
- A new locale is one more column plus one line in the locale list. A missing
  string falls back to English.
- **The grid constraint:** a translation must not break the layout. Russian
  is noticeably longer than English (`RUN` takes 3 cells, its Russian translation 6). So every row of
  the table carries a maximum width in cells, and the self-test fails the
  build if any translation exceeds it.
- JetBrains Mono covers 122 of 256 Cyrillic codepoints: basic Russian is
  there. A locale outside the font's coverage needs another font.

---

## 15. Themes as files

Themes live in **`themes/`** next to the launcher, together with
`resources.dat` and `data.bin` (§10). The format is JSON, one file per theme.

### File format

```json
{
  "name": "Phosphor",
  "author": "ConUI",
  "version": 1,
  "colors": {
    "bg.window":      "#062038",
    "bg.panel":       "#0B3055",
    "bg.raised":      "#124070",
    "bg.sunken":      "#03182A",
    "bg.selection":   "#1FA35C",
    "border.dim":     "#1B5585",
    "border":         "#E8C14A",
    "border.focus":   "#FFE07A",
    "text.primary":   "#EAF2FA",
    "text.secondary": "#9CC4E4",
    "text.muted":     "#5E87AC",
    "text.inverse":   "#03182A",
    "accent":         "#4AE8E8",
    "accent.dim":     "#2A8C9C",
    "warn":           "#E8C14A",
    "danger":         "#FF6B6B",
    "success":        "#7CE89A",
    "info":           "#8AC8FF",
    "shadow":         "#0000008C",
    "dither":         "#124070",
    "retro.filled":   "#2A8C9C",
    "retro.empty":    "#5E87AC",
    "sidebar.bg":     "#124070",
    "sidebar.icon":   "#5E87AC",
    "sidebar.hover":  "#EAF2FA",
    "sidebar.active": "#4AE8E8",
    "table.colsep":   "#1B5585",
    "table.rowsep":   "#1B5585",
    "chart.1":        "#4AE8E8",
    "chart.2":        "#E8C14A",
    "chart.3":        "#7CE89A",
    "chart.4":        "#C49AFF"
  }
}
```

- Keys are exactly the slot names from §5. An unknown key is ignored with a
  warning; it does not fail the load.
- A colour is `#RRGGBB` or `#RRGGBBAA`; without alpha, `FF` is assumed.
- `name` is what the theme list shows. The file name is independent of it;
  a new theme's file name is derived from its name (lower case, anything
  other than letters, digits, `-` and `_` becomes `-`).

### Load order

1. The **built-in** themes (`PHOSPHOR`, `PAPER`, `MIDNIGHT`) are registered.
   They are compiled into the binary, so the application always has
   something to draw with.
2. If `themes/` does not exist, it is created and copies of the built-ins
   are written into it, so there is something to open and start from.
3. `themes/*.json` is scanned. A theme with the same `name` as a built-in
   **replaces** it.
4. A slot missing from a file comes from the built-in theme of the same
   name, otherwise from `PHOSPHOR`. A theme with a single changed colour is a
   valid file.
5. **A broken file never stops the application**: it is skipped, and the file
   name and the line of the error are reported on `stderr` (visible when the
   application runs from a terminal) and kept in `ThemeStore::warnings()`.

### Parser

Its own, on a strict subset of JSON: objects, strings, numbers,
`true/false/null`, shallow nesting. No external libraries (§12). Hand-written
parsers are a known source of bugs, so:

- any deviation from the schema is a clear error with a line number, never
  silently accepted garbage;
- the self-test covers a truncated file, an unterminated string, garbage
  bytes, duplicate keys, nesting too deep and escapes;
- writing and reading are checked by a round-trip test on every built-in
  theme.

---

## 16. Raster elements

The system draws itself with glyphs and rectangles, but an application built
on it must be able to show a real picture: a file thumbnail, a result
preview, a logo, a chart drawn by something else. A separate layer exists for
that: **sprites**.

### What a sprite is

```c
struct SpriteRef { float x, y, w, h; float alpha; uint16_t id; uint8_t z; ClipRect clip; uint8_t tint; };
```

A rectangle in cell fractions plus a raster id. Rasters live in their own
store (`RasterStore`), loaded from `resources.dat` or from a file, and go into
a separate RGBA atlas page, not the glyph atlas, because that one is
single-channel (`R8`) and is rebuilt whenever the density changes.

### Rules

1. **The box is whole cells, the content is not.** A sprite gets its
   rectangle in cells from the layout; what happens inside is not the grid's
   business. The picture is scaled bilinearly into that rectangle.
2. **No system widget uses sprites, except the caption boxes** (§8.1) **and
   the sidebar icons** (§8.18), where the application supplies the picture
   by its nature. Buttons, tree icons, arrows and markers are glyphs. Anywhere
   else a sprite appears only where application code explicitly puts it.
3. **A sprite can be a mask tinted with a theme slot** (`SpriteRef::tint`):
   the picture's colour is multiplied by the slot's colour, and its alpha
   stays its own. A white mask with `tint = danger` draws red and changes
   colour with the theme. Without a tint (`kNoTint`) the picture draws as is.
4. **Its own pair of layers.** A content sprite draws above the window's
   glyph cells but under the overlays; a sprite inside an overlay draws above
   them (§2). A menu that covers a preview really covers it.
5. **Still one draw call.** The raster page is a second texture **unit**, not
   a second batch: the mode travels in the vertex, the shader picks the
   texture, and the layer order is the vertex order. The cost of the layer is
   one 1024² RGBA texture (4 MB) and a branch in the shader. A second call
   would only be needed for a picture that does not fit in the page.
6. **Alpha is supported**; a raster without an alpha channel is opaque.
7. **Formats**: raw RGBA and anything `stb_image.h` decodes (PNG, JPEG and
   others). `stb_image.h` loads application pictures; it is not part of UI
   rendering.

### What the layer does not have

Rotation, nine-patches, filters, sprite animation and atlas animation. If any
of these is needed, it is a separate decision, not "the raster is there
anyway".

**The exception: a picture the application draws itself.**
`RasterStore::update(id, rgba)` redraws a raster in place, at the size it was
added with. The renderer sends only the changed rectangle to the GPU, not the
whole page, and the page does not fill up with old frames. This is not sprite
animation in the layer: the layer has no frames, no frame switching and no
timing; application code builds every frame and asks for the next one itself
(`ctx.animate`). The playground's optional ABOUT cat uses it (built with
`CONUI_ABOUT_CAT=ON`, the default); it is a demonstration, not part of the
design system.

In the playground, the `PREVIEW` row on the WIDGETS tab shows a `108 × 80`
RGBA picture in a `6 × 2` cell box: the one place on that screen where
pixels come neither from the font nor from the geometry layer.
