# ConUI API map

A condensed map of the API with working patterns taken from the playground.
The authoritative signatures are in `src/UI/Widgets/Widgets.hpp`,
`src/UI/Context.hpp`, `src/UI/Layout.hpp`, `src/Render/CellBuffer.hpp` and
`src/App/App.hpp`. Section numbers (`§8.8`) refer to `docs/DESIGN_SYSTEM.md`.

## Includes and namespaces

```cpp
#include "App.hpp"
#include "../Core/ThemeStore.hpp"
#include "../Core/Utils.hpp"          // AppState, Density
#include "../Core/Json.hpp"           // json::Parse / Write for SaveState
#include "../Core/IconSheets.hpp"     // LoadIconStrip (sidebar icons)
#include "../Render/CellBuffer.hpp"
#include "../UI/Context.hpp"
#include "../UI/Glyphs.hpp"           // Gly::H, Gly::FULL, … (§4)
#include "../UI/Layout.hpp"           // ui::Split
#include "../UI/Strings.hpp"          // str::T, str::ID_…
#include "../UI/Theme.hpp"            // slots: TEXT, ACCENT, DANGER, …
#include "../UI/WindowChrome.hpp"     // chrome::CONTENT_TOP, chrome::DrawRule
#include "../UI/Widgets/Widgets.hpp"

using namespace ui;                   // widgets live in ui::, strings in str::
```

## The application contract (`src/App/App.hpp`)

```cpp
namespace app {
const char* Title();                          // caption text
void MainSize(int& cols, int& rows);          // first-run size, in cells
const char* SecondTitle();                    // nullptr: no second window
void SecondSize(int& cols, int& rows);
void Init(AppServices& s);                    // once: load rasters, open files
void MainFrame(ui::Context& ctx, AppServices& s, AppActions& a);
void SecondFrame(ui::Context& ctx, AppServices& s, AppActions& a);
void LoadState(const std::string& data);      // what SaveState returned last run
std::string SaveState();                      // called by the autosave and on exit
}
```

`AppServices`: `themes`, `state` (AppState, persisted), `layout` (split
fractions), `rasters`, `themeIndex`, `fps`, `lastVerts`, `fullscreen`.
`AppActions`: `quit`, `theme`, `density`, `toggle_fullscreen`,
`open_second`, `close_second`. Set them; the loop acts after the frame.

## Frame skeleton

```cpp
void MainFrame(Context& ctx, AppServices& s, AppActions& a) {
    CellBuffer& cb = ctx.cells();
    const int W = cb.width(), H = cb.height();
    const int top = s.fullscreen ? 0 : chrome::CONTENT_TOP;   // caption rows
    if (W < 60 || H < top + 12) {                             // too small: say so
        if (H > top) cb.text(1, top, "window too small", TEXT_MUTED, BG_WINDOW);
        return;
    }
    ctx.pushId("myapp");

    menuBar(ctx, s, a, top);                 // row `top`
    chrome::DrawRule(cb, top + 1);           // rule under the menu bar

    const int bodyH = (H - 3) - (top + 2) + 1;
    const Rect body{ 1, top + 2, W - 2, bodyH };   // add SIDEBAR_W + 1 to x with a sidebar
    Rect left, right;
    Split(ctx, s.layout, "main", Axis::Cols, body, 0.35f, 24, 40, left, right);
    panelList(ctx, left);
    panelDetail(ctx, right);

    ctx.popId();

    chrome::DrawRule(cb, H - 2, /*raised=*/true);   // rule above the status bar
    statusBar(cb, s);                                // row H-1
}
```

## Layout

- `Rect{x, y, w, h}` is in cells, half-open. `r.inset(1)`, `r.contains(px, py)`.
- `Split(ctx, s.layout, "name", Axis::Cols | Axis::Rows, r, defaultFraction,
  minA, minB, a, b)`: two panes with a draggable one-cell seam; nesting calls
  nests the tree. The name is the persistent key in `data.bin`.
- Inside a region (a panel, a tab, a modal): a cursor in cells.
  - `ctx.allocate(w, h)` reserves cells at the cursor, returns the `Rect`;
  - `ctx.allocateRow(h)` takes the rest of the line;
  - `ctx.newline(n)`, `ctx.spacer(cells)`, `ctx.column(x)`;
  - `ctx.regionWidth()`, `ctx.region()`, `ctx.cursorX()/cursorY()`;
  - `ctx.pushRegion(r)` / `ctx.popRegion()` for a custom sub-area.

## Panels, tabs, scrolling

```cpp
Rect in = BeginPanel(ctx, "files", str::T(str::ID_FILES), r);           // framed section
// optional: status text in the top border, PANEL_FLUSH for full-width lists
Rect in2 = BeginPanel(ctx, "fs", str::T(str::ID_FS), r, nullptr, PANEL_FLUSH);
EndPanel(ctx);

const char* tabs[3] = { str::T(str::ID_A), str::T(str::ID_B), str::T(str::ID_C) };
Rect tin = BeginTabs(ctx, "controls", r, tabs, 3, state.tab);
BeginScroll(ctx, "scroll", tin, 1, 0);        // content taller than the tab scrolls
if (state.tab == 0) tabA(ctx); else if (state.tab == 1) tabB(ctx); else tabC(ctx);
EndScroll(ctx);
EndTabs(ctx);
```

## Forms: a label column, a control, a value on the right

```cpp
const int kLabelW = 11;                      // fits the longest label in every locale
LabelFixed(ctx, str::T(str::ID_OPACITY), kLabelW);
Slider(ctx, "opacity", barW, s.state.opacity, 0.20f, 1.0f);
Tooltip(ctx, str::T(str::ID_TIP_OPACITY));   // attaches to the last widget
char buf[16]; std::snprintf(buf, sizeof buf, "%d%%", (int)std::lround(s.state.opacity * 100));
LabelRight(ctx, buf, TEXT, ATTR_BOLD);
ctx.newline();

LabelFixed(ctx, str::T(str::ID_PORT), kLabelW);
TextInput(ctx, "port", fieldW, state.port, nullptr, valid ? TXT_NONE : TXT_ERROR);
ctx.newline();

LabelFixed(ctx, str::T(str::ID_DENSITY), kLabelW);
if (Dropdown(ctx, "density", fieldW, items, n, state.sel)) applyDensity(state.sel);
ctx.newline();

if (Checkbox(ctx, str::T(str::ID_AUTOSAVE), state.autosave)) { /* changed */ }
Switch(ctx, str::T(str::ID_BLUR), state.blur);
Radio(ctx, "Option A", state.choice, 0);  Radio(ctx, "Option B", state.choice, 1);
```

Buttons: `Button(ctx, str::ID_RUN, BTN_DEFAULT)` (the primary action),
`BTN_DANGER` for irreversible ones, `ctx.beginDisabled(cond)` /
`ctx.endDisabled()` around disabled ones. Put `ctx.spacer(1)` between buttons.

Progress: `ProgressBar(ctx, w, v, ACCENT)`, `ProgressRetro(ctx, w, v)`,
`ProgressBusy(ctx, w)` (indeterminate, animates itself).

## Text

- `Label(ctx, text, slot, attr)`; `LabelRight`; `LabelFixed`.
- `TextInput(ctx, id, w, std::string&, placeholder, flags, &history)` returns
  `TXT_CHANGED | TXT_SUBMITTED`.
- `TextArea(ctx, id, title, r, std::string&)`: a framed multi-line editor
  with line numbers and its own status bar.
- Direct drawing when no widget fits: `cb.text(x, y, s, fg, bg, attr)`,
  `cb.textRight(xRight, y, s, fg, bg)`, `cb.put(x, y, codepoint, fg, bg)`,
  `cb.bgRect(x, y, w, h, bg)`, `cb.fill(...)`. Use `BG_PANEL` inside panels and
  `BG_WINDOW` on bare window: those are layer colours and paint nothing
  themselves.

## Tables

```cpp
const char* procCell(void* user, int row, int col) {       // read-only cells
    static char buf[32];
    const State& d = *(const State*)user;
    const Proc& p = d.procs[d.order[row]];                 // rows are in the caller's order
    switch (col) {
    case 0: std::snprintf(buf, sizeof buf, "%d", p.pid); return buf;
    case 1: return p.name.c_str();
    default: return "";
    }
}

TableColumn cols[2] = { { str::T(str::ID_COL_PID), 5, true },   // title, width, right-aligned
                        { str::T(str::ID_COL_NAME), 12 } };       // the last column stretches
const int ev = Table(ctx, "procs", str::T(str::ID_PROCESSES), r, cols, 2,
                     (int)d.order.size(), procCell, &d, d.sel, d.sortCol, d.sortDesc);
if (ev & TBL_SORT) sortProcs(d);             // the widget never sorts
if (ev & TBL_ACTIVATE) openProc(d.sel);      // Enter or double click
```

`TBL_BARE` drops the frame (the header is the top row, the bottom row is a
margin). Editable cells: `EditTable` with `std::string* cell(void*, row, col)`
(`nullptr` = read-only) and the events `ET_EDITED`, `ET_SORT`, `ET_INSERT`,
`ET_DELETE` with the row in `evRow`; the caller inserts, deletes and sorts
(see `panelTasks` in `src/App/Playground.cpp`).

## Tree

```cpp
std::vector<TreeNode> roots;                 // label, meta, children, open, dir
const TreeNode* selected = nullptr;
Rect in = BeginPanel(ctx, "fs", str::T(str::ID_FILESYSTEM), r, nullptr, PANEL_FLUSH);
int ev = Tree(ctx, "tree", in, roots, selected);   // TREE_SELECT | TREE_TOGGLE | TREE_ACTIVATE
EndPanel(ctx);
```

## Charts and diagrams

```cpp
const float rx[12] = { … }, tx[12] = { … };
const ChartSeries series[2] = { { "rx", rx, CHART_1 }, { "tx", tx, CHART_2 } };
static const char* const hours[12] = { "00", "02", … };
LineChart(ctx, "traffic", str::T(str::ID_TRAFFIC), r, series, 2, 12, hours);       // fits the range
LineChart(ctx, "cpu", "CPU", r, cpu, 1, n, labels, 0.0f, 100.0f, CHART_GRID | CHART_SMOOTH);
BarChart(ctx, "disk", "DISK", r, series, 1, n, labels, 0.0f, CHART_GRID | CHART_HORIZONTAL);
```

A live chart asks for its next frame: `ctx.animate(secondsUntilNextSample)`.

```cpp
std::vector<DiagramNode> nodes = { { "÷", -1 }, { "+", 0 }, { "×", 0 } };   // parents first
const int hov = TreeDiagram(ctx, "expr", inside, nodes.data(), (int)nodes.size());
if (hov >= 0) Tooltip(ctx, tipFor(hov));
NetDiagram(ctx, "net", inside, layers, nLayers, weights);    // weights[k]: n(k+1) × n(k), row-major
ResetDiagram(ctx, "expr");                                   // after loading new content
```

## Menus, modals, tooltips

```cpp
// Menu bar: titles in one row, each opens a menu drawn in the same id scope.
ctx.pushRegion(Rect{ 1, top, W - 2, 1 });
MenuBarItem(ctx, str::T(str::ID_MENU_FILE), "m.file"); ctx.spacer(1);
MenuBarItem(ctx, str::T(str::ID_MENU_HELP), "m.help");
ctx.popRegion();
const Rect none{ 0, 0, 0, 0 };                // bar menus open from the bar only
if (BeginContextMenu(ctx, "m.file", none)) {
    if (MenuItem(ctx, str::T(str::ID_M_OPEN), "Ctrl+O")) openFile();
    MenuSeparator(ctx);
    if (MenuItem(ctx, str::T(str::ID_M_QUIT), "Ctrl+Q")) a.quit = true;
    EndContextMenu(ctx);
}

// Right-click menu over an area
if (BeginContextMenu(ctx, "rowmenu", tableRect)) {
    if (MenuItem(ctx, "Rename…", "F2")) rename();
    if (BeginSubmenu(ctx, "Sort by")) { MenuCheck(ctx, "Name", byName); EndSubmenu(ctx); }
    if (MenuItem(ctx, "Delete", "Del", MENU_DANGER)) state.confirm = true;
    EndContextMenu(ctx);
}

// Destructive confirmation: danger frame, CANCEL is the default
if (BeginModal(ctx, "confirm", str::T(str::ID_CONFIRM), 50, 9, state.confirm, MODAL_DANGER)) {
    Label(ctx, str::T(str::ID_CONFIRM_Q), TEXT, ATTR_BOLD);
    ctx.newline(3);
    ctx.column(std::max(0, ctx.regionWidth() - 24));
    if (Button(ctx, str::ID_DELETE, BTN_DANGER)) { doDelete(); state.confirm = false; }
    ctx.spacer(2);
    if (Button(ctx, str::ID_CANCEL, BTN_DEFAULT)) state.confirm = false;
    EndModal(ctx);
}
```

Overlays can be called anywhere in the frame; they always draw on top.
`OpenContextMenu(ctx, id, x, y)` opens a menu from code (a shortcut).

## Sidebar

```cpp
const IconStrip* ic = LoadIconStrip(s.rasters, "myicons", ctx.input().cell_h, 3);
SidebarItem items[3];
for (int i = 0; i < 3; ++i) { items[i].icon = ic ? ic->ids[i] : -1; items[i].tip = str::T(kNames[i]); }
Sidebar(ctx, "nav", Rect{ 0, top + 2, SIDEBAR_W, bodyH }, items, 3, state.screen,
        ic ? ic->px : 0, 1.0f - RULE_MID, RULE_MID);   // reaches the rules above and below
```

Icon sheets `myicons_<N>.png` (white masks, N × N squares in a row, one file
per pixel size) go into `src/assets/myicons/`; `tools/secicons.cpp` shows how
to generate them.

## Status bar

```cpp
void statusBar(CellBuffer& cb, AppServices& s) {
    const int W = cb.width(), H = cb.height();
    char badge[32];
    std::snprintf(badge, sizeof badge, " %s ", str::T(str::ID_READY));
    int x = 2 + cb.text(2, H - 1, badge, TEXT_INV, BG_SEL, ATTR_BOLD) + 2;   // inverted state badge
    x += cb.text(x, H - 1, "F1 help", TEXT2, BG_WINDOW) + 3;                 // key hints
    cb.textRight(W - 3, H - 1, s.themes.at(s.themeIndex).name.c_str(), TEXT_MUTED, BG_WINDOW);
}
```

## Context services

- Identity: `ctx.pushId("name")` / `ctx.pushId(i)` / `ctx.popId()` around
  repeated content (list rows, per-item widgets).
- Hover, focus, press for custom widgets: `Id id = ctx.makeId("x");
  ctx.focusable(id); bool clicked = ctx.interact(id, rect);
  ctx.isHot(id)`, `ctx.isFocused(id)`, `ctx.isPressed(id)`,
  `ctx.focusActivated(id)`; `ctx.setCursor(Cursor::Pointer)` while hot.
- Input this frame: `ctx.input()` (mouse in cells, keys, `time`, `text`).
- Per-widget memory: `ctx.state(id)` (a float), `ctx.blob<T>(id)` (a struct).
- Animation: `ctx.animate(seconds)`; blink phase `ctx.blinkOn()`.
- Glitches: `ctx.glitchScreen(Context::ScreenGlitch::Tear)` for a failure;
  `Glitch(ctx, id, rect, error)` after a custom widget.
- Clipboard: `ctx.setClipboard(text)`; pasted text arrives in
  `ctx.input().paste` with `key_paste`.

## State in data.bin

```cpp
std::string SaveState() {
    using namespace json;
    Members m;
    m.push_back({ "screen", Num(g.screen) });
    m.push_back({ "query", Str(g.query) });
    return Write(Obj(std::move(m)));
}
void LoadState(const std::string& src) {
    if (src.empty()) return;                                 // first run
    json::Value v; json::Error err;
    if (!json::Parse(src, v, err) || !v.isObject()) return;  // damaged: keep defaults
    int i = 0;
    if (v.getInt("screen", i) && i >= 0 && i <= 2) g.screen = i;   // range-check every value
    v.getString("query", g.query);
}
```

## Rasters

```cpp
size_t n = 0;
const void* p = ResourceManager::Get().GetResource("logo.png", n);   // listed in RES_SRC_FILES
const int logo = s.rasters.addFromMemory((const uint8_t*)p, n);       // in Init()
cb.sprite(x, y, w, h, (uint16_t)logo);                                // in a frame, cells as floats
```

A picture redrawn every frame: `addRGBA` once, then `s.rasters.update(id, rgba)`.
