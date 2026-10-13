# ConUI design rules: checklist

Check a ConUI screen against this list before calling it done. Section
numbers refer to `docs/DESIGN_SYSTEM.md`.

## Grid and layout

- [ ] Every element occupies whole cells. Sizes and margins are counted in
      cells, never pixels (§1).
- [ ] The window follows the standard frame: caption row 0 (core), row 1
      empty, menu bar at `top`, a rule under it, the body, a raised rule at
      `H-2`, the status bar at `H-1` (§8.1).
- [ ] Panels are arranged with `Split`, not placed at absolute positions that
      leave gaps. Every split has sensible minimums (§8.17).
- [ ] Content starts on the row right under a panel title; no blank row
      between them (§8.7).
- [ ] A panel's first and last inner columns stay free (margin and
      scrollbar column); full-width lists use `PANEL_FLUSH` (§8.7).
- [ ] The screen degrades on a small window: below a minimum size it shows
      "window too small" instead of overlapping widgets.

## Colour

- [ ] Only palette slots, never RGB literals (§5).
- [ ] One accent (`accent`). `danger` only for irreversible actions, errors and
      the close box. `warn`, `success`, `info` only for those states.
- [ ] Selection is `bg.selection` with `text.inverse` across the full row (§9).
- [ ] Inside panels, cells use `BG_PANEL`; on the bare window, `BG_WINDOW`.
      Own backgrounds only for `bg.raised`, `bg.sunken`, `bg.selection`.
- [ ] Chart series use `chart.1`..`chart.4` in order.

## Glyphs and shapes

- [ ] Glyphs come from the vocabulary in `src/UI/Glyphs.hpp` (§4).
- [ ] Single frame lines only: no `═ ║ ╔`, no shadows, no rounded panels.
- [ ] No new shapes. `cb.shape`/`cb.segment` are reserved for the elements
      listed in §2.1; a new widget is made of glyphs.
- [ ] No emoji, no characters the font lacks (Braille, `◢◣◤◥`, CJK).

## Typography

- [ ] One font, one size. Headings and labels in UPPER CASE; data as is (§7).
- [ ] Hierarchy by inversion, case and `text.primary` / `text.secondary` /
      `text.muted`. Bold sparingly (values, the selected item).
- [ ] Numbers in tables are right-aligned; column titles are left-aligned
      (§8.8).

## Interaction

- [ ] Everything works from the keyboard: `Tab` order follows reading order,
      arrows move inside lists, `Enter` activates, `Esc` closes overlays (§9).
- [ ] The primary action of a screen or dialog is `BTN_DEFAULT`; an
      irreversible one is `BTN_DANGER`, and its confirmation is a
      `MODAL_DANGER` modal whose default button is CANCEL (§8.9).
- [ ] Custom clickable elements set `Cursor::Pointer` while hot; draggable
      pictures set `Cursor::Move` (§9).
- [ ] Disabled controls use `ctx.beginDisabled()`: muted, no input, out of the
      `Tab` order.
- [ ] A field in error uses `TXT_ERROR` (red text, `✗`, a one-off glitch); a
      fault of the whole application may use
      `ctx.glitchScreen(Context::ScreenGlitch::Tear)`. Glitches respect
      `AppState::glitches`.
- [ ] Anything that changes with time calls `ctx.animate(…)` every frame
      (§9).

## Data ownership and state

- [ ] Widgets get references to the application's values; tables and trees
      read the application's data and report events; the application sorts,
      inserts and deletes (§8.8, §8.19).
- [ ] Panel and widget ids are constant strings, separate from localised
      titles.
- [ ] State that should survive a restart goes through `SaveState()` /
      `LoadState()`, with every value range-checked on load.

## Strings

- [ ] Every visible label is a `str::ID_…` in the string table with a width
      budget, and fits that budget in every locale (§14).
- [ ] Form label columns are as wide as the longest label in every locale.

## Build

- [ ] `./build.sh` passes, including `--selftest`.
- [ ] The application ran, and the user confirmed the screen.
