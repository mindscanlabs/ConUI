#include "Playground.hpp"
#ifdef CONUI_ABOUT_CAT
#include "AboutCat.hpp"   // optional: cmake -DCONUI_ABOUT_CAT=OFF leaves it out
#endif
#include "App.hpp"
#include "Scene.hpp"
#include "ThemeEditor.hpp"
#include "../Core/IconSheets.hpp"
#include "../Core/Json.hpp"
#include "../Core/RasterStore.hpp"
#include "../Core/ThemeStore.hpp"
#include "../Core/Utils.hpp"
#include "../Render/CellBuffer.hpp"
#include "../UI/Context.hpp"
#include "../UI/Glyphs.hpp"
#include "../UI/Layout.hpp"
#include "../UI/Strings.hpp"
#include "../UI/Theme.hpp"
#include "../UI/WindowChrome.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace ui;

namespace {

// ============================================================================
// Demo data
// ============================================================================

void initData(PlaygroundState& d) {
    static const char* kNames[] = {
        "conui.exe", "build.exe", "ninja.exe", "cc1plus.exe", "ld.exe", "cmake.exe",
        "explorer.exe", "dwm.exe", "svchost.exe", "code.exe", "git.exe", "bash.exe",
    };
    for (int i = 0; i < 40; ++i) {
        PlaygroundState::Proc p;
        p.pid = 1000 + i * 37 % 911 * 7;
        p.name = kNames[i % 12];
        p.cpu = (float)((i * 53) % 173) / 10.0f;
        p.mem = 3.5f + (float)((i * 97) % 1400) / 10.0f;
        p.state = (i * 7) % 3;
        d.procs.push_back(p);
    }
    d.order.resize(d.procs.size());
    for (size_t i = 0; i < d.order.size(); ++i) d.order[i] = (int)i;

    auto leaf = [](const char* n, const char* m = "") {
        TreeNode t; t.label = n; t.meta = m; return t;
    };
    auto dir = [](const char* n, std::vector<TreeNode> kids, bool open = false) {
        TreeNode t; t.label = n; t.children = std::move(kids); t.dir = true; t.open = open; return t;
    };
    std::vector<TreeNode> refs;
    for (int i = 1; i <= 13; ++i) {
        char b[16];
        std::snprintf(b, sizeof b, "R%02d.png", i);
        refs.push_back(leaf(b, "96K"));
    }
    d.fs.push_back(dir("conui", {
        dir("docs", { leaf("REFERENCES.md", "4.1K"), leaf("DESIGN_SYSTEM.md", "9.7K"),
                      leaf("IMPLEMENTATION_PLAN.md", "31K"), leaf("FORK.md", "5.2K") }, true),
        dir("refs", refs),
        dir("src", {
            dir("App", { leaf("Playground.cpp", "24K"), leaf("Skeleton.cpp", "3.1K"),
                         leaf("ThemeEditor.cpp", "11K") }),
            dir("Core", { leaf("AppWindow.cpp", "9.1K"), leaf("Json.cpp", "12K"),
                          leaf("ThemeStore.cpp", "8.4K") }),
            dir("Render", { leaf("CellBuffer.cpp", "3.9K"), leaf("Renderer.cpp", "15K") }),
            dir("UI", { dir("Widgets", { leaf("Menu.cpp", "9.8K"), leaf("Table.cpp", "9.0K"),
                                         leaf("Tree.cpp", "7.6K") }),
                        leaf("Context.cpp", "12K"), leaf("Layout.cpp", "6.1K") }, true),
        }, true),
        leaf("CMakeLists.txt", "6.8K"),
        leaf("build.sh", "1.9K"),
    }, true));

    const char* kTasks[][3] = {
        { "Glyph atlas", "ceo", "Rasterise once,\ncache per codepoint" },
        { "Sidebar", "ceo", "Icons from resources.dat" },
        { "Glitches", "anna", "Tear, jolt, noise\nOff in SETTINGS\nOne at a time" },
        { "Theme editor", "max", "" },
        { "Editable table", "ceo", "Enter / F2 edits\nShift+Enter: new line" },
    };
    for (const auto& t : kTasks) d.tasks.push_back({ d.task_next_id++, { t[0], t[1], t[2] } });

    // The sample text only on a first run: once NOTES has been saved, even
    // as empty, it is the user's.
    if (!d.notes_loaded)
        d.notes = "First line of the document.\n"
                  "Second line: click, type, select with Shift.\n"
                  "\n"
                  "Ctrl+A/C/X/V, Insert toggles overwrite.\n"
                  "Строка на кириллице — по кодпоинтам.";
    d.ready = true;
}

const char* kStateNames[] = { "RUNNING", "IDLE", "SLEEP" };

const char* procCell(void* user, int row, int col) {
    static char buf[32];
    const PlaygroundState& d = *(const PlaygroundState*)user;
    const PlaygroundState::Proc& p = d.procs[(size_t)d.order[(size_t)row]];
    switch (col) {
    case 0: std::snprintf(buf, sizeof buf, "%d", p.pid); break;
    case 1: return p.name.c_str();
    case 2: std::snprintf(buf, sizeof buf, "%.1f%%", p.cpu); break;
    case 3: std::snprintf(buf, sizeof buf, "%.1f MB", p.mem); break;
    default: return kStateNames[p.state];
    }
    return buf;
}

void sortProcs(PlaygroundState& d) {
    // The selection follows its ROW, not its position: re-sorting must not
    // silently select a different process.
    const int keep = d.proc_sel >= 0 ? d.order[(size_t)d.proc_sel] : -1;
    const int col = d.sort_col;
    const bool desc = d.sort_desc;
    std::stable_sort(d.order.begin(), d.order.end(), [&](int a, int b) {
        const PlaygroundState::Proc& x = d.procs[(size_t)a];
        const PlaygroundState::Proc& y = d.procs[(size_t)b];
        bool lt = false, gt = false;
        switch (col) {
        case 0: lt = x.pid < y.pid; gt = x.pid > y.pid; break;
        case 1: lt = x.name < y.name; gt = x.name > y.name; break;
        case 2: lt = x.cpu < y.cpu; gt = x.cpu > y.cpu; break;
        case 3: lt = x.mem < y.mem; gt = x.mem > y.mem; break;
        default: lt = x.state < y.state; gt = x.state > y.state; break;
        }
        return desc ? gt : lt;
    });
    for (size_t i = 0; i < d.order.size(); ++i)
        if (d.order[i] == keep) d.proc_sel = (int)i;
}

/** 1 .. 65535, digits only — what the PORT field validates against. */
bool portValid(const std::string& s) {
    if (s.empty() || s.size() > 5) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    const long v = std::strtol(s.c_str(), nullptr, 10);
    return v >= 1 && v <= 65535;
}

// ============================================================================
// Workspace: CONTROLS (three tabs), BUILD, FILESYSTEM, PROCESSES, NOTES
// ============================================================================

const int kLabelW = 11;         // fits the longest form label in either locale

/**
 * The two opacity rows, as WIDGETS and SETTINGS both show them: OPACITY is
 * the window's real alpha, PANELS the sections' own, independent of it (§6).
 */
void opacityRows(Context& ctx, AppServices& s, int barW) {
    struct Row { str::Id label, tip; const char* id; float& v; float lo; };
    const Row rows[] = { { str::ID_OPACITY, str::ID_TIP_OPACITY, "opacity", s.state.opacity, 0.20f },
                         { str::ID_PANEL_OPACITY, str::ID_TIP_PANEL_OPACITY, "panel_opacity",
                           s.state.panel_opacity, 0.0f } };
    char buf[16];
    for (const Row& row : rows) {
        LabelFixed(ctx, str::T(row.label), kLabelW);
        Slider(ctx, row.id, barW, row.v, row.lo, 1.0f);
        Tooltip(ctx, str::T(row.tip));
        std::snprintf(buf, sizeof buf, "%d%%", (int)std::lround(row.v * 100.0f));
        LabelRight(ctx, buf, TEXT, ATTR_BOLD);
        ctx.newline();
    }
}

void tabWidgets(Context& ctx, AppServices& s, PlaygroundState& d, int rasterId) {
    CellBuffer& cb = ctx.cells();
    const int W = ctx.regionWidth();
    const int barW = std::max(6, W - kLabelW - 9);
    char buf[48];

    // buttons: idle, default and disabled on one row, (§8.2)
    Button(ctx, str::ID_RUN, BTN_DEFAULT);
    ctx.spacer(1);
    Button(ctx, str::ID_STOP);
    ctx.spacer(1);
    ctx.beginDisabled();
    Button(ctx, str::ID_RESET);
    ctx.endDisabled();
    ctx.newline(2);

    Checkbox(ctx, str::T(str::ID_OPT_TRANSPARENT), d.transparent);
    ctx.newline();
    Checkbox(ctx, str::T(str::ID_OPT_GRID), d.grid);
    ctx.newline();
    CheckboxTri(ctx, str::T(str::ID_OPT_AUTOSAVE), d.autosave);
    ctx.newline(2);

    LabelFixed(ctx, str::T(str::ID_BLUR), kLabelW);
    Switch(ctx, nullptr, d.blur);
    ctx.newline(2);

    opacityRows(ctx, s, barW);
    LabelFixed(ctx, str::T(str::ID_STEPS), kLabelW);
    Slider(ctx, "steps", barW, d.steps, 0.0f, 8.0f, 1.0f);
    std::snprintf(buf, sizeof buf, "%d/8", (int)std::lround(d.steps));
    LabelRight(ctx, buf, TEXT, ATTR_BOLD);
    ctx.newline();
    LabelFixed(ctx, str::T(str::ID_RANGE), kLabelW);
    SliderRange(ctx, "range", barW, d.range_lo, d.range_hi, 0.0f, 100.0f, 1.0f);
    std::snprintf(buf, sizeof buf, "%d … %d", (int)d.range_lo, (int)d.range_hi);
    LabelRight(ctx, buf, TEXT, ATTR_BOLD);
    ctx.newline(2);

    if (Button(ctx, str::ID_DELETE_DOTS, BTN_DANGER)) d.confirm = true;   // the window jolts as it opens
    ctx.spacer(1);
    // A simulated fault: the whole window tears (§9)
    if (Button(ctx, str::ID_FAULT, BTN_DANGER)) ctx.glitchScreen(Context::ScreenGlitch::Tear);
    ctx.newline();

    // a raster element (§16) in the same layout as everything else
    if (rasterId >= 0) {
        LabelFixed(ctx, str::T(str::ID_PREVIEW), kLabelW);
        const Rect pr = ctx.allocate(6, 2);
        cb.shape(pr.x - 1.0f / 9.0f, pr.y - 1.0f / 20.0f,
                 6.0f + 2.0f / 9.0f, 2.0f + 2.0f / 20.0f, BORDER_DIM, 0.0f, 1.0f, 1.0f);
        cb.sprite((float)pr.x, (float)pr.y, 6.0f, 2.0f, (uint16_t)rasterId);
        ctx.spacer(2);
        Label(ctx, "108×80 RGBA", TEXT_MUTED);
        ctx.newline(2);
    }
}

void tabInput(Context& ctx, PlaygroundState& d) {
    const int fieldW = std::max(8, ctx.regionWidth() - kLabelW);
    LabelFixed(ctx, str::T(str::ID_NAME), kLabelW);
    TextInput(ctx, "name", fieldW, d.name);
    ctx.newline();
    LabelFixed(ctx, str::T(str::ID_SEARCH), kLabelW);
    TextInput(ctx, "search", fieldW, d.search, str::T(str::ID_PH_SEARCH), TXT_NONE, &d.history);
    ctx.newline();
    LabelFixed(ctx, str::T(str::ID_PORT), kLabelW);
    TextInput(ctx, "port", fieldW, d.port, nullptr, portValid(d.port) ? TXT_NONE : TXT_ERROR);
    ctx.newline();
}

/** The app's real settings (§5, §6, §14, §3): each control drives the window. */
void tabSettings(Context& ctx, AppServices& s, AppActions& a) {
    const int fieldW = std::max(10, std::min(ctx.regionWidth() - kLabelW, 24));

    std::vector<const char*> names;
    for (int i = 0; i < s.themes.count(); ++i) names.push_back(s.themes.at(i).name.c_str());
    LabelFixed(ctx, str::T(str::ID_MENU_THEME), kLabelW);
    int t = s.themeIndex;
    if (Dropdown(ctx, "theme", fieldW, names.data(), (int)names.size(), t)) a.theme = t;
    ctx.newline();

    const char* locales[str::LOCALE_COUNT];
    for (int i = 0; i < str::LOCALE_COUNT; ++i) locales[i] = str::LocaleName(i);
    LabelFixed(ctx, str::T(str::ID_LOCALE), kLabelW);
    int loc = str::GetLocale();
    if (Dropdown(ctx, "locale", fieldW, locales, str::LOCALE_COUNT, loc)) {
        str::SetLocale(loc);
        s.state.locale = str::LocaleCode(loc);
    }
    ctx.newline();

    // Smallest to largest on screen; the Density numbers are data.bin's and
    // not in that order (Tiny was appended), so the list maps between them.
    const Density kDensOrder[] = { Density::Tiny, Density::Compact, Density::Normal, Density::Large };
    const str::Id kDens[] = { str::ID_DENS_TINY, str::ID_DENS_COMPACT, str::ID_DENS_NORMAL,
                              str::ID_DENS_LARGE };
    const char* dens[4];
    int dn = 2;
    for (int i = 0; i < 4; ++i) {
        dens[i] = str::T(kDens[i]);
        if (kDensOrder[i] == s.state.density) dn = i;
    }
    LabelFixed(ctx, str::T(str::ID_DENSITY), kLabelW);
    if (Dropdown(ctx, "density", fieldW, dens, 4, dn)) a.density = (int)kDensOrder[dn];
    ctx.newline();

    opacityRows(ctx, s, std::max(6, ctx.regionWidth() - kLabelW - 6));
    ctx.newline();

    // Every glitch, element and whole-window, off in one switch (§9)
    bool glitches = s.state.glitches != 0;
    if (Checkbox(ctx, str::T(str::ID_OPT_GLITCH), glitches)) s.state.glitches = glitches ? 1 : 0;
    ctx.newline(2);

    if (Button(ctx, str::ID_ED_OPEN_EDITOR)) a.open_second = true;
    ctx.newline();
}

void panelBuild(Context& ctx, PlaygroundState& d, const Rect& r) {
    // Three bars; the middle one moves unless frozen.
    // The middle bar creeps with the clock, so the frame asks for another —
    // 30 Hz: it crosses the bar in eight seconds.
    if (!d.frozen) ctx.animate(1.0 / 30.0);
    const float res = d.frozen ? 0.643f : (float)std::fmod(ctx.input().time / 8.0, 1.0);
    BeginPanel(ctx, "build", str::T(str::ID_BUILD), r, "3/4");
    const int barW = std::max(6, ctx.regionWidth() - kLabelW - 6);
    struct Row { str::Id label; float v; uint8_t slot; };
    const Row rows[] = { { str::ID_COMPILE, 1.0f, SUCCESS }, { str::ID_RESOURCES, res, ACCENT },
                         { str::ID_LINK, 0.0f, TEXT_MUTED } };
    char buf[16];
    for (const Row& row : rows) {
        LabelFixed(ctx, str::T(row.label), kLabelW);
        ProgressBar(ctx, barW, row.v, row.slot);
        std::snprintf(buf, sizeof buf, "%d%%", (int)(row.v * 100.0f));
        LabelRight(ctx, buf, TEXT, ATTR_BOLD);
        ctx.newline();
    }
    ctx.newline();
    LabelFixed(ctx, str::T(str::ID_TESTS), kLabelW);
    ProgressRetro(ctx, barW, 14.0f / 22.0f);
    LabelRight(ctx, "14/22", TEXT, ATTR_BOLD);
    ctx.newline();
    LabelFixed(ctx, str::T(str::ID_PENDING), kLabelW, TEXT_MUTED);
    ProgressBusy(ctx, barW);
    EndPanel(ctx);
}

void panelTree(Context& ctx, PlaygroundState& d, const Rect& r) {
    const Rect in = BeginPanel(ctx, "fs", str::T(str::ID_FILESYSTEM), r, nullptr, PANEL_FLUSH);
    Tree(ctx, "tree", in, d.fs, d.fs_sel);
    EndPanel(ctx);

    // the context menu (§8.10), right-click anywhere on the tree
    if (BeginContextMenu(ctx, "fsmenu", r)) {
        // The file actions are the menu's look only: the demo tree has no files.
        MenuItem(ctx, str::T(str::ID_M_OPEN), "Enter");
        MenuItem(ctx, str::T(str::ID_M_RENAME), "F2");
        MenuItem(ctx, str::T(str::ID_M_COPY_PATH), "Ctrl+C");
        MenuSeparator(ctx);
        MenuCheck(ctx, str::T(str::ID_M_SHOW_HIDDEN), d.show_hidden);
        if (BeginSubmenu(ctx, str::T(str::ID_M_SORT_BY))) {
            static const str::Id kBy[] = { str::ID_M_BY_NAME, str::ID_M_BY_SIZE, str::ID_M_BY_DATE };
            for (int i = 0; i < 3; ++i) {
                bool on = d.sort_by == i;
                if (MenuCheck(ctx, str::T(kBy[i]), on)) d.sort_by = i;
            }
            EndSubmenu(ctx);
        }
        MenuSeparator(ctx);
        if (MenuItem(ctx, str::T(str::ID_M_DELETE), "Del", MENU_DANGER)) d.confirm = true;
        EndContextMenu(ctx);
    }
}

void panelTable(Context& ctx, PlaygroundState& d, const Rect& r) {
    static const TableColumn kCols[] = {
        { "PID", 5, true }, { "NAME", 12, false }, { "CPU", 6, true },
        { "MEM", 9, true }, { "STATE", 7, false },
    };
    TableColumn cols[5];
    const str::Id titles[5] = { str::ID_COL_PID, str::ID_NAME, str::ID_COL_CPU,
                                str::ID_COL_MEM, str::ID_COL_STATE };
    for (int i = 0; i < 5; ++i) { cols[i] = kCols[i]; cols[i].title = str::T(titles[i]); }
    const int ev = Table(ctx, "procs", str::T(str::ID_PROCESSES), r, cols, 5,
                         (int)d.order.size(), procCell, &d, d.proc_sel, d.sort_col, d.sort_desc,
                         TBL_BARE);   // no frame, title or count: just the grid (§8.8)
    if (ev & TBL_SORT) sortProcs(d);
}

std::string* taskCell(void* user, int row, int col) {
    return &((PlaygroundState*)user)->tasks[(size_t)row].cell[col];
}

void sortTasks(PlaygroundState& d) {
    // As with PROCESSES, the current cell follows its row through the sort.
    const int keep = d.task_row >= 0 && d.task_row < (int)d.tasks.size() ? d.tasks[(size_t)d.task_row].id : -1;
    const int col = d.task_sort;
    const bool desc = d.task_desc;
    std::stable_sort(d.tasks.begin(), d.tasks.end(), [&](const PlaygroundState::Task& x,
                                                         const PlaygroundState::Task& y) {
        return desc ? x.cell[col] > y.cell[col] : x.cell[col] < y.cell[col];
    });
    for (size_t i = 0; i < d.tasks.size(); ++i)
        if (d.tasks[i].id == keep) d.task_row = (int)i;
}

void panelTasks(Context& ctx, PlaygroundState& d, const Rect& r) {
    const EditColumn cols[3] = { { str::T(str::ID_COL_TASK), 14 }, { str::T(str::ID_COL_OWNER), 6 },
                                 { str::T(str::ID_COL_NOTES), 16 } };
    int evRow = -1;
    const int ev = EditTable(ctx, "tasks", str::T(str::ID_TASKS), r, cols, 3, (int)d.tasks.size(),
                             taskCell, &d, d.task_row, d.task_col, d.task_sort, d.task_desc, evRow);
    if (ev & ET_SORT) sortTasks(d);
    if (ev & ET_INSERT) {
        const int at = std::clamp(evRow, 0, (int)d.tasks.size());
        d.tasks.insert(d.tasks.begin() + at, PlaygroundState::Task{ d.task_next_id++, {} });
        d.task_row = at;
    }
    if ((ev & ET_DELETE) && evRow >= 0 && evRow < (int)d.tasks.size()) {
        d.tasks.erase(d.tasks.begin() + evRow);
        d.task_row = std::min(d.task_row, (int)d.tasks.size() - 1);
    }
}

void screenWorkspace(Context& ctx, AppServices& s, AppActions& a, PlaygroundState& d,
                     int rasterId, const Rect& body) {
    // The split tree of the Workspace screen (§8.17):
    //   ws.main (cols) ── ws.left (rows) ── CONTROLS / BUILD
    //                 └── ws.mid  (cols) ── ws.fs (rows) ── FILESYSTEM / TASKS
    //                                    └── ws.right (rows) ── PROCESSES / NOTES
    Rect left, rest, ctrl, build, mid, tree, tasks, right, table, notes;
    Split(ctx, s.layout, "ws.main", Axis::Cols, body, 0.31f, 32, 50, left, rest);
    Split(ctx, s.layout, "ws.left", Axis::Rows, left, 0.70f, 12, 8, ctrl, build);
    Split(ctx, s.layout, "ws.mid", Axis::Cols, rest, 0.45f, 22, 30, mid, right);
    Split(ctx, s.layout, "ws.fs", Axis::Rows, mid, 0.50f, 8, 8, tree, tasks);
    Split(ctx, s.layout, "ws.right", Axis::Rows, right, 0.58f, 8, 7, table, notes);

    const char* tabs[3] = { str::T(str::ID_MENU_WIDGETS), str::T(str::ID_TAB_INPUT),
                            str::T(str::ID_TAB_SETTINGS) };
    const Rect in = BeginTabs(ctx, "controls", ctrl, tabs, 3, d.tab);
    BeginScroll(ctx, "scroll", in, 1, 0);
    if (d.tab == 0)      tabWidgets(ctx, s, d, rasterId);
    else if (d.tab == 1) tabInput(ctx, d);
    else                 tabSettings(ctx, s, a);
    EndScroll(ctx);
    EndTabs(ctx);

    panelBuild(ctx, d, build);
    panelTree(ctx, d, tree);
    panelTasks(ctx, d, tasks);
    panelTable(ctx, d, table);
    TextArea(ctx, "notes", str::T(str::ID_NOTES), notes, d.notes);
}

// ============================================================================
// Charts (§8.20): lines straight and smooth, bars vertical and horizontal
// ============================================================================

const int kCpuSamples = 40;

/** One more CPU sample: two waves and a ripple, so the curve has something to do. */
void pushCpuSample(PlaygroundState& d) {
    const float k = (float)d.cpu_tick++;
    d.cpu_user.push_back(38.0f + 22.0f * std::sin(k * 0.31f) + 9.0f * std::sin(k * 1.27f + 0.6f));
    d.cpu_sys.push_back(14.0f + 7.0f * std::sin(k * 0.47f + 1.0f) + 3.0f * std::sin(k * 2.03f));
    if ((int)d.cpu_user.size() > kCpuSamples) {
        d.cpu_user.erase(d.cpu_user.begin());
        d.cpu_sys.erase(d.cpu_sys.begin());
    }
}

void screenCharts(Context& ctx, AppServices& s, PlaygroundState& d, const Rect& body) {
    // The live chart moves on every half second — unless frozen for a snapshot.
    const double now = ctx.input().time;
    while ((int)d.cpu_user.size() < kCpuSamples) pushCpuSample(d);
    if (!d.frozen && now >= d.cpu_next) { pushCpuSample(d); d.cpu_next = now + 0.5; }
    if (!d.frozen) ctx.animate(std::max(0.0, d.cpu_next - now));   // the next sample

    Rect top, bottom, tl, tr, bl, br;
    Split(ctx, s.layout, "ch.main", Axis::Rows, body, 0.5f, 9, 9, top, bottom);
    Split(ctx, s.layout, "ch.top", Axis::Cols, top, 0.5f, 26, 26, tl, tr);
    Split(ctx, s.layout, "ch.bottom", Axis::Cols, bottom, 0.5f, 26, 26, bl, br);

    // TRAFFIC — two series, straight runs, the hour along the bottom.
    static const char* kHours[] = { "00", "02", "04", "06", "08", "10", "12", "14", "16", "18", "20", "22" };
    static const float kRx[] = { 12, 9, 7, 8, 21, 46, 58, 52, 61, 74, 49, 27 };
    static const float kTx[] = { 4, 3, 3, 5, 11, 19, 27, 22, 30, 33, 21, 9 };
    const ChartSeries traffic[2] = { { "rx", kRx, CHART_1 }, { "tx", kTx, CHART_2 } };
    LineChart(ctx, "traffic", str::T(str::ID_CH_TRAFFIC), tl, traffic, 2, 12, kHours);

    // CPU LOAD — live, smoothed, on a fixed 0..100 %.
    const int n = (int)d.cpu_user.size();
    std::vector<std::string> ago((size_t)n);
    std::vector<const char*> agoPtr((size_t)n);
    for (int i = 0; i < n; ++i) {
        const int sec = (n - 1 - i) / 2;
        ago[(size_t)i] = sec ? "-" + std::to_string(sec) + "s" : "0s";
        agoPtr[(size_t)i] = ago[(size_t)i].c_str();
    }
    const ChartSeries cpu[2] = { { "user", d.cpu_user.data(), CHART_1 },
                                 { "sys", d.cpu_sys.data(), CHART_3 } };
    LineChart(ctx, "cpu", str::T(str::ID_CH_CPU), tr, cpu, 2, n, agoPtr.data(), 0.0f, 100.0f,
              CHART_SMOOTH | CHART_GRID);

    // COMMITS — grouped vertical bars, two years by month.
    static const char* kMonths[] = { "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12" };
    static const float kY1[] = { 34, 41, 28, 55, 62, 48, 39, 22, 57, 71, 66, 45 };
    static const float kY2[] = { 49, 52, 61, 58, 77, 83, 64, 41, 72, 88, 0, 0 };
    const ChartSeries commits[2] = { { "2025", kY1, CHART_1 }, { "2026", kY2, CHART_2 } };
    BarChart(ctx, "commits", str::T(str::ID_CH_WEEKLY), bl, commits, 2, 12, kMonths);

    // DISK USAGE — horizontal bars, the project's own folders.
    static const char* kDirs[] = { "src", "docs", "refs", "build", "dist", "tools", "themes" };
    static const float kUsed[] = { 4.2f, 1.1f, 12.6f, 38.4f, 21.7f, 0.9f, 0.1f };
    static const float kCache[] = { 0.0f, 0.0f, 3.1f, 17.9f, 6.2f, 0.4f, 0.0f };
    const ChartSeries disk[2] = { { "MB", kUsed, CHART_3 }, { "cache", kCache, CHART_4 } };
    BarChart(ctx, "disk", str::T(str::ID_CH_DISK), br, disk, 2, 7, kDirs, 0.0f,
             CHART_HORIZONTAL | CHART_GRID);
}

// ============================================================================
// Diagrams (§8.21): a tree of boxes and a layered network, zoomed and moved
// ============================================================================

void screenDiagrams(Context& ctx, AppServices& s, PlaygroundState& d, const Rect& body) {
    Rect left, right;
    Split(ctx, s.layout, "dg.main", Axis::Cols, body, 0.5f, 30, 30, left, right);

    // EXPRESSION TREE — the larger root of a quadratic, (−b + √(b² − 4ac)) / 2a,
    // in pre-order: operations in the chart colours by kind, variables in
    // `accent`, numbers muted. Hovering an operation shows what it computes.
    struct Cube { const char* label; int parent; uint8_t slot; const char* tip; };
    static const Cube kExpr[] = {
        { "÷", -1, CHART_1, "(−b + √(b² − 4ac)) / 2a" },
        { "+",  0, CHART_1, "−b + √(b² − 4ac)" },
        { "−x", 1, CHART_1, "−b" },
        { "b",  2, ACCENT,  nullptr },
        { "√",  1, CHART_2, "√(b² − 4ac)" },
        { "−",  4, CHART_1, "b² − 4ac" },
        { "^",  5, CHART_2, "b²" },
        { "b",  6, ACCENT,  nullptr },
        { "2",  6, TEXT_MUTED, nullptr },
        { "×",  5, CHART_1, "4ac" },
        { "×",  9, CHART_1, "4a" },
        { "4", 10, TEXT_MUTED, nullptr },
        { "a", 10, ACCENT,  nullptr },
        { "c",  9, ACCENT,  nullptr },
        { "×",  0, CHART_1, "2a" },
        { "2", 14, TEXT_MUTED, nullptr },
        { "a", 14, ACCENT,  nullptr },
    };
    const int nExpr = (int)(sizeof kExpr / sizeof kExpr[0]);
    std::vector<DiagramNode> expr((size_t)nExpr);
    for (int i = 0; i < nExpr; ++i) {
        expr[(size_t)i].label = kExpr[i].label;
        expr[(size_t)i].parent = kExpr[i].parent;
        expr[(size_t)i].slot = kExpr[i].slot;
    }
    const Rect tin = BeginPanel(ctx, "dg.tree", str::T(str::ID_DG_TREE), left);
    const int hov = TreeDiagram(ctx, "expr", tin, expr.data(), nExpr);
    if (hov >= 0 && kExpr[hov].tip) Tooltip(ctx, kExpr[hov].tip);
    EndPanel(ctx);

    // NETWORK — three inputs, two hidden layers of six, one output; the
    // weights a fixed pattern of both signs and every strength.
    const int widths[4] = { 3, 6, 6, 1 };
    if (d.net_w[0].empty())
        for (int L = 0; L < 3; ++L)
            for (int j = 0; j < widths[L + 1]; ++j)
                for (int i = 0; i < widths[L]; ++i)
                    d.net_w[L].push_back(std::sin(i * 1.7f + j * 2.3f + L * 0.9f) *
                                         (0.3f + 0.7f * std::fabs(std::cos(i * 0.6f + j * 1.1f))));
    static const char* kIn[] = { "n", "r", "R" };
    static const char* kOut[] = { "P" };
    const NetLayer layers[4] = { { 3, ACCENT, kIn }, { 6, TEXT2 }, { 6, TEXT2 }, { 1, SUCCESS, kOut } };
    const float* weights[3] = { d.net_w[0].data(), d.net_w[1].data(), d.net_w[2].data() };
    const Rect nin = BeginPanel(ctx, "dg.net", str::T(str::ID_DG_NET), right);
    NetDiagram(ctx, "net", nin, layers, 4, weights);
    EndPanel(ctx);
}

// ============================================================================
// States: every widget in every state, each drawing itself (Context::beginForced)
// ============================================================================

void screenStates(Context& ctx, const Rect& body) {
    CellBuffer& cb = ctx.cells();
    const Rect in = BeginPanel(ctx, "states", str::T(str::ID_STATES), body);
    const int labelW = 14;
    const int colW = std::max(12, (ctx.regionWidth() - labelW) / 6);
    const str::Id heads[6] = { str::ID_ST_IDLE, str::ID_ST_HOVER, str::ID_ST_ACTIVE,
                               str::ID_ST_FOCUS, str::ID_ST_DISABLED, str::ID_ST_ERROR };
    // header
    LabelFixed(ctx, str::T(str::ID_ST_WIDGET), labelW, TEXT_MUTED);
    for (int c = 0; c < 6; ++c) {
        ctx.column(labelW + c * colW);
        Label(ctx, str::T(heads[c]), TEXT2, ATTR_BOLD);
    }
    ctx.newline();
    {
        const Rect r = ctx.allocateRow(1);
        for (int x = r.x; x < r.x + r.w; ++x) cb.put(x, r.y, Gly::H, BORDER_DIM, BG_PANEL);
        ctx.newline();
    }

    enum Kind { K_BTN, K_DEF, K_DANGER, K_CHECK, K_RADIO, K_SWITCH, K_SLIDER, K_TEXT, K_DROP, K_COUNT };
    const str::Id names[K_COUNT] = { str::ID_W_BUTTON, str::ID_W_DEFAULT, str::ID_W_DANGER,
                                     str::ID_W_CHECKBOX, str::ID_W_RADIO, str::ID_W_SWITCH,
                                     str::ID_W_SLIDER, str::ID_W_TEXT, str::ID_W_DROPDOWN };
    static const int kForce[6] = { FORCE_NONE, FORCE_HOT, FORCE_HOT | FORCE_PRESSED,
                                   FORCE_FOCUS, FORCE_NONE, FORCE_NONE };
    static const char* kItems[] = { "Normal", "Large" };

    for (int k = 0; k < K_COUNT; ++k) {
        LabelFixed(ctx, str::T(names[k]), labelW, TEXT2);
        for (int c = 0; c < 6; ++c) {
            ctx.column(labelW + c * colW);
            const bool err = c == 5;
            // Only the text field has an error state; the rest say so.
            if (err && k != K_TEXT) { Label(ctx, "—", TEXT_MUTED); continue; }
            ctx.pushId(k * 16 + c);
            ctx.beginDisabled(c == 4);
            ctx.beginForced(kForce[c]);
            // Values are locals: nothing here can change them, it is a picture.
            bool on = true;
            int sel = 0;
            float v = 0.6f;
            std::string txt = "conui";
            switch (k) {
            case K_BTN:    Button(ctx, str::ID_RUN); break;
            case K_DEF:    Button(ctx, str::ID_RUN, BTN_DEFAULT); break;
            case K_DANGER: Button(ctx, str::ID_DELETE, BTN_DANGER); break;
            case K_CHECK:  Checkbox(ctx, "On", on); break;
            case K_RADIO:  Radio(ctx, "A", sel, 0); break;
            case K_SWITCH: Switch(ctx, nullptr, on); break;
            case K_SLIDER: Slider(ctx, "s", colW - 2, v); break;
            case K_TEXT:   TextInput(ctx, "t", colW - 2, txt, nullptr, err ? TXT_ERROR : TXT_NONE); break;
            case K_DROP:   Dropdown(ctx, "d", colW - 2, kItems, 2, sel); break;
            default: break;
            }
            ctx.endForced();
            ctx.endDisabled();
            ctx.popId();
        }
        ctx.newline(2);
    }
    (void)in;
    EndPanel(ctx);
}

// ============================================================================
// Glyphs: the U+2500–259F sheet and the vocabulary (§4) — the old --chart
// ============================================================================

void screenGlyphs(Context& ctx, AppServices& s, const Rect& body) {
    CellBuffer& cb = ctx.cells();
    Rect sheet, vocab;
    Split(ctx, s.layout, "gl.main", Axis::Cols, body, 0.52f, 44, 30, sheet, vocab);

    // U+2500–259F: box drawing and block elements, sixteen to a row. The
    // renderer draws these from the font, scaled to the cell (§3) — this is
    // the sheet to look at when checking that they join without seams.
    BeginPanel(ctx, "sheet", "U+2500–259F", sheet);
    {
        Rect h = ctx.allocateRow(1);
        cb.text(h.x, h.y, "      ", TEXT_MUTED, BG_PANEL);
        for (int i = 0; i < 16; ++i) {
            char hx[4];
            std::snprintf(hx, sizeof hx, "%X", i);
            cb.text(h.x + 6 + i * 2, h.y, hx, TEXT_MUTED, BG_PANEL);
        }
        ctx.newline();
        for (int row = 0; row < 10; ++row) {
            const Rect r = ctx.allocateRow(1);
            char base[8];
            std::snprintf(base, sizeof base, "%04X", 0x2500 + row * 16);
            cb.text(r.x, r.y, base, TEXT_MUTED, BG_PANEL);
            for (int i = 0; i < 16; ++i)
                cb.put(r.x + 6 + i * 2, r.y, (uint32_t)(0x2500 + row * 16 + i), TEXT, BG_PANEL);
            ctx.newline();
        }
        ctx.newline();
        // A solid field of █ — any seam between cells shows up here first.
        LabelFixed(ctx, str::T(str::ID_SOLID), 16, TEXT2);
        ctx.newline();
        for (int j = 0; j < 3; ++j) {
            const Rect r = ctx.allocate(std::min(32, ctx.regionWidth()), 1);
            for (int i = 0; i < r.w; ++i) cb.put(r.x + i, r.y, Gly::FULL, ACCENT, BG_PANEL);
            ctx.newline();
        }
        ctx.newline();
        LabelFixed(ctx, str::T(str::ID_SHADES), 16, TEXT2);
        ctx.newline();
        {
            const Rect r = ctx.allocate(32, 1);
            const uint32_t shades[4] = { Gly::SHADE_25, Gly::SHADE_50, Gly::SHADE_75, Gly::FULL };
            for (int b = 0; b < 4; ++b)
                for (int i = 0; i < 8; ++i) cb.put(r.x + b * 8 + i, r.y, shades[b], TEXT, BG_PANEL);
            ctx.newline();
            const Rect e = ctx.allocate(20, 1);
            for (int i = 1; i <= 8; ++i) cb.put(e.x + i - 1, e.y, Gly::LeftEighths(i), TEXT, BG_PANEL);
            for (int i = 1; i <= 8; ++i) cb.put(e.x + 10 + i - 1, e.y, Gly::LowerEighths(i), ACCENT, BG_PANEL);
        }
    }
    EndPanel(ctx);

    // The vocabulary: every glyph the widgets may use (§4), scrollable.
    const Rect vin = BeginPanel(ctx, "vocab", str::T(str::ID_VOCAB), vocab, nullptr, PANEL_FLUSH);
    BeginScroll(ctx, "scroll", vin, 1, 1);
    const int per = std::max(1, (ctx.regionWidth()) / 11);
    for (int i = 0; i < Gly::kAllCount; ++i) {
        if (i > 0 && i % per == 0) ctx.newline();
        const Rect r = ctx.allocate(11, 1);
        char code[12];
        std::snprintf(code, sizeof code, "%04X", Gly::kAll[i]);
        cb.put(r.x, r.y, Gly::kAll[i], ACCENT, BG_PANEL);
        cb.text(r.x + 2, r.y, code, TEXT_MUTED, BG_PANEL);
    }
    ctx.newline();
    EndScroll(ctx);
    EndPanel(ctx);
}

// ============================================================================
// The menu bar and the status bar
// ============================================================================

void menuBar(Context& ctx, AppServices& s, AppActions& a, PlaygroundState& d, int y) {
    CellBuffer& cb = ctx.cells();
    const int W = cb.width();
    ctx.pushRegion(Rect{ 1, y, W - 2, 1 });
    MenuBarItem(ctx, str::T(str::ID_MENU_FILE), "m.file");
    ctx.spacer(1);
    MenuBarItem(ctx, str::T(str::ID_MENU_VIEW), "m.view");
    ctx.spacer(1);
    MenuBarItem(ctx, str::T(str::ID_MENU_WIDGETS), "m.widgets");
    ctx.spacer(1);
    MenuBarItem(ctx, str::T(str::ID_MENU_THEME), "m.theme");
    ctx.spacer(1);
    MenuBarItem(ctx, str::T(str::ID_MENU_HELP), "m.help");
    ctx.popRegion();

    char right[96];
    std::snprintf(right, sizeof right, "%s · %d×%d · %s %d%%", s.themes.at(s.themeIndex).name.c_str(),
                  W, cb.height(), str::T(str::ID_OPACITY), (int)std::lround(s.state.opacity * 100.0f));
    if (Utf8Width(right) + 50 < W) cb.textRight(W - 2, y, right, TEXT_MUTED, BG_WINDOW);

    const Rect none{ 0, 0, 0, 0 };          // bar menus open from the bar only
    if (BeginContextMenu(ctx, "m.file", none)) {
        if (MenuItem(ctx, str::T(str::ID_M_DELETE), "Del", MENU_DANGER)) d.confirm = true;
        MenuSeparator(ctx);
        if (MenuItem(ctx, str::T(str::ID_M_QUIT), "Ctrl+Q")) a.quit = true;
        EndContextMenu(ctx);
    }
    if (BeginContextMenu(ctx, "m.view", none)) {
        const str::Id kScr[] = { str::ID_VIEW_WORKSPACE, str::ID_VIEW_STATES, str::ID_VIEW_GLYPHS,
                                 str::ID_VIEW_CHARTS, str::ID_VIEW_DIAGRAMS };
        for (int i = 0; i < 5; ++i) {
            bool on = d.screen == i;
            if (MenuCheck(ctx, str::T(kScr[i]), on)) d.screen = i;
        }
        MenuSeparator(ctx);
        if (MenuItem(ctx, str::T(str::ID_M_FULLSCREEN), "F11")) a.toggle_fullscreen = true;
        EndContextMenu(ctx);
    }
    if (BeginContextMenu(ctx, "m.widgets", none)) {
        const str::Id kTab[] = { str::ID_M_TAB_CONTROLS, str::ID_M_TAB_INPUT, str::ID_M_TAB_SETTINGS };
        for (int i = 0; i < 3; ++i) {
            bool on = d.screen == 0 && d.tab == i;
            if (MenuCheck(ctx, str::T(kTab[i]), on)) { d.screen = 0; d.tab = i; }
        }
        EndContextMenu(ctx);
    }
    if (BeginContextMenu(ctx, "m.theme", none)) {
        for (int i = 0; i < s.themes.count(); ++i) {
            ctx.pushId(i);
            bool on = i == s.themeIndex;
            if (MenuCheck(ctx, s.themes.at(i).name.c_str(), on)) a.theme = i;
            ctx.popId();
        }
        MenuSeparator(ctx);
        if (MenuItem(ctx, str::T(str::ID_M_THEME_EDITOR))) a.open_second = true;
        EndContextMenu(ctx);
    }
    if (BeginContextMenu(ctx, "m.help", none)) {
        if (MenuItem(ctx, str::T(str::ID_M_ABOUT), "F1")) d.about = true;
        EndContextMenu(ctx);
    }
}

void statusBar(CellBuffer& cb, AppServices& s) {
    const int W = cb.width(), H = cb.height();
    chrome::DrawRule(cb, H - 2, /*raised=*/true);
    int x = 2;
    char badge[32];
    std::snprintf(badge, sizeof badge, " %s ", str::T(str::ID_READY));
    x += cb.text(x, H - 1, badge, TEXT_INV, BG_SEL, ATTR_BOLD) + 2;
    const str::Id hints[] = { str::ID_HINT_F1, str::ID_HINT_FULLSCREEN, str::ID_HINT_QUITKEY };
    char metrics[96];
    std::snprintf(metrics, sizeof metrics, str::T(str::ID_FMT_METRICS),
                  (int)std::lround(s.fps), (int)s.lastVerts);
    const int mw = Utf8Width(metrics) + 2;
    for (str::Id h : hints) {
        const int hw = Utf8Width(str::T(h));
        if (x + hw + mw + 4 >= W) break;
        x += cb.text(x, H - 1, str::T(h), TEXT2, BG_WINDOW) + 3;
    }
    // Real numbers: the loop measures the frame rate and the renderer counts
    // the vertices of its one draw call, not a placeholder.
    if (x + mw + 2 < W) {
        cb.textRight(W - 5, H - 1, metrics, ACCENT_DIM, BG_WINDOW);
        cb.put(W - 3, H - 1, Gly::DIAMOND, ACCENT_DIM, BG_WINDOW);
    }
}

} // namespace

// ============================================================================

void BuildPlayground(Context& ctx, AppServices& s, AppActions& a, PlaygroundState& d,
                     int rasterId) {
    CellBuffer& cb = ctx.cells();
    const int W = cb.width(), H = cb.height();
    // No caption in fullscreen, so nothing
    // reserves its rows either — the menu bar moves up to row 0.
    const int top = s.fullscreen ? 0 : chrome::CONTENT_TOP;
    if (W < 60 || H < top + 12) {
        if (H > top) cb.text(1, top, "window too small", TEXT_MUTED, BG_WINDOW);
        return;
    }
    if (!d.ready) { initData(d); sortProcs(d); }

    const Input& in = ctx.input();
    if (in.key_f1) d.about = true;

    ctx.pushId("playground");
    menuBar(ctx, s, a, d, top);
    chrome::DrawRule(cb, top + 1);

    // Down to the row above the status rule: the rule is H-2, the status
    // bar is the last row, H-1 — nothing is left empty under it.
    const int bodyH = (H - 3) - (top + 2) + 1;

    // The sidebar (§8.18): the same five screens as the VIEW menu, between
    // the rule under the menu and the rule over the status bar. Its icons are
    // a strip from resources.dat, one row tall at this window's cell size.
    {
        const Rect side{ 0, top + 2, SIDEBAR_W, bodyH };
        const IconStrip* ic = LoadIconStrip(s.rasters, "sections", in.cell_h, 5);
        const str::Id kScr[] = { str::ID_VIEW_WORKSPACE, str::ID_VIEW_STATES, str::ID_VIEW_GLYPHS,
                                 str::ID_VIEW_CHARTS, str::ID_VIEW_DIAGRAMS };
        SidebarItem items[5];
        for (int i = 0; i < 5; ++i) {
            items[i].icon = ic ? ic->ids[(size_t)i] : -1;
            items[i].tip = str::T(kScr[i]);
        }
        // Rule above (under the menu) and rule below (over the status bar):
        // the ground reaches each rule's middle, so the strip runs rule to rule.
        // The rule below is drawn half a row up (§8.1), so its middle is half
        // a row higher too.
        Sidebar(ctx, "sections", side, items, 5, d.screen, ic ? ic->px : 0,
                1.0f - RULE_MID, RULE_MID - 0.5f);
    }

    const Rect body{ SIDEBAR_W + 1, top + 2, W - SIDEBAR_W - 2, bodyH };
    ctx.pushId(d.screen);
    switch (d.screen) {
    case 1:  screenStates(ctx, body); break;
    case 2:  screenGlyphs(ctx, s, body); break;
    case 3:  screenCharts(ctx, s, d, body); break;
    case 4:  screenDiagrams(ctx, s, d, body); break;
    default: screenWorkspace(ctx, s, a, d, rasterId, body); break;
    }
    ctx.popId();

    // ---- modals: drawn in the overlay grid, so their place in the frame
    // does not matter; they take the input from the frame after they open.
    if (BeginModal(ctx, "confirm", str::T(str::ID_CONFIRM), 50, 9, d.confirm, MODAL_DANGER)) {
        Label(ctx, str::T(str::ID_CONFIRM_Q), TEXT, ATTR_BOLD);
        ctx.newline();
        Label(ctx, str::T(str::ID_CONFIRM_NOTE), TEXT2);
        ctx.newline(3);
        ctx.column(std::max(0, ctx.regionWidth() - 24));
        if (Button(ctx, str::ID_DELETE, BTN_DANGER)) d.confirm = false;
        ctx.spacer(2);
        // CANCEL is the default: an Enter out of habit must not delete.
        if (Button(ctx, str::ID_CANCEL, BTN_DEFAULT)) d.confirm = false;
        EndModal(ctx);
    }
    // The cat (optional, CONUI_ABOUT_CAT; not part of the design system): a
    // 30 x 10 card over the text, centred — a layer of its own, a character to
    // a quarter of the cell, so four times the detail each way. A window too
    // low for it gets the text alone, and so does a build without it.
#ifdef CONUI_ABOUT_CAT
    const int catW = 30, catH = H - 2 - 10 - 1 >= 10 ? 10 : 0;
#else
    const int catH = 0;
#endif
    if (BeginModal(ctx, "about", str::T(str::ID_ABOUT_TITLE), 56, 10 + (catH ? catH + 1 : 0), d.about)) {
#ifdef CONUI_ABOUT_CAT
        if (catH) {
            ctx.column(std::max(0, (ctx.regionWidth() - catW) / 2));
            DrawAboutCat(ctx, &s.rasters, &s.themes.at(s.themeIndex),
                         ctx.allocate(std::min(catW, ctx.regionWidth()), catH), 0.25f,
                         ctx.input().time, d.frozen);
            ctx.newline();                              // under the cat: its height
            ctx.newline();                              // and a row of air
        }
#endif
        Label(ctx, str::T(str::ID_ABOUT_1), TEXT, ATTR_BOLD);
        ctx.newline();
        Label(ctx, str::T(str::ID_ABOUT_2), TEXT2);
        ctx.newline(2);
        Label(ctx, str::T(str::ID_ABOUT_3), TEXT_MUTED);
        ctx.newline(2);
        ctx.column(std::max(0, ctx.regionWidth() - 8));
        if (Button(ctx, str::ID_OK, BTN_DEFAULT)) d.about = false;
        EndModal(ctx);
    }
    ctx.popId();

    statusBar(cb, s);
}

// ============================================================================
// The playground's part of data.bin
// ============================================================================

std::string SavePlaygroundState(const PlaygroundState& d) {
    using namespace json;
    std::vector<Value> hist;
    // The newest 20 entries: history is a convenience, not a log.
    const size_t from = d.history.size() > 20 ? d.history.size() - 20 : 0;
    for (size_t i = from; i < d.history.size(); ++i) hist.push_back(Str(d.history[i]));
    Members m;
    m.emplace_back("screen",      Num(d.screen));
    m.emplace_back("tab",         Num(d.tab));
    m.emplace_back("name",        Str(d.name));
    m.emplace_back("search",      Str(d.search));
    m.emplace_back("port",        Str(d.port));
    m.emplace_back("history",     Arr(std::move(hist)));
    m.emplace_back("notes",       Str(d.notes));
    m.emplace_back("transparent", Bool(d.transparent));
    m.emplace_back("grid",        Bool(d.grid));
    m.emplace_back("blur",        Bool(d.blur));
    m.emplace_back("autosave",    Num(d.autosave));
    m.emplace_back("steps",       Num(d.steps));
    m.emplace_back("range_lo",    Num(d.range_lo));
    m.emplace_back("range_hi",    Num(d.range_hi));
    m.emplace_back("sort_col",    Num(d.sort_col));
    m.emplace_back("sort_desc",   Bool(d.sort_desc));
    m.emplace_back("show_hidden", Bool(d.show_hidden));
    m.emplace_back("sort_by",     Num(d.sort_by));
    return Write(Obj(std::move(m)), 0);
}

void LoadPlaygroundState(PlaygroundState& d, const std::string& src) {
    if (src.empty()) return;                     // first run: the defaults stand
    json::Value v;
    json::Error err;
    if (!json::Parse(src, v, err) || !v.isObject()) return;   // damaged: defaults
    // Every read is optional and range-checked: a key missing from an older
    // file keeps its default, a key from a newer one is simply not read.
    int i = 0;
    double x = 0;
    if (v.getInt("screen", i) && i >= 0 && i <= 4) d.screen = i;
    if (v.getInt("tab", i) && i >= 0 && i <= 2) d.tab = i;
    v.getString("name", d.name);
    v.getString("search", d.search);
    v.getString("port", d.port);
    if (v.getString("notes", d.notes)) d.notes_loaded = true;
    if (const json::Value* h = v.get("history"); h && h->type == json::Value::Type::Array) {
        d.history.clear();
        for (const json::Value& e : h->elements)
            if (e.isString()) d.history.push_back(e.string);
    }
    v.getBool("transparent", d.transparent);
    v.getBool("grid", d.grid);
    v.getBool("blur", d.blur);
    if (v.getInt("autosave", i) && i >= 0 && i <= 2) d.autosave = i;
    if (v.getNumber("steps", x) && x >= 0 && x <= 8) d.steps = (float)x;
    double lo = 0, hi = 0;
    if (v.getNumber("range_lo", lo) && v.getNumber("range_hi", hi) &&
        lo >= 0 && hi <= 100 && lo <= hi) {
        d.range_lo = (float)lo;
        d.range_hi = (float)hi;
    }
    if (v.getInt("sort_col", i) && i >= 0 && i <= 4) d.sort_col = i;
    v.getBool("sort_desc", d.sort_desc);
    v.getBool("show_hidden", d.show_hidden);
    if (v.getInt("sort_by", i) && i >= 0 && i <= 2) d.sort_by = i;
}

// ============================================================================
// The application interface (App.hpp) — the playground build
// ============================================================================

namespace {
PlaygroundState  g_pg;
ThemeEditorState g_editor;
int              g_raster = -1;
}

namespace app {

const char* Title() { return "CONUI · PLAYGROUND"; }
void MainSize(int& cols, int& rows) { cols = 124; rows = 41; }
const char* SecondTitle() { return "THEME EDITOR"; }
void SecondSize(int& cols, int& rows) { cols = 60; rows = 34; }    // §8.16

void Init(AppServices& s) { g_raster = MakeDemoRaster(s.rasters); }

void LoadState(const std::string& data) { LoadPlaygroundState(g_pg, data); }
std::string SaveState() { return SavePlaygroundState(g_pg); }

void MainFrame(Context& ctx, AppServices& s, AppActions& a) {
    BuildPlayground(ctx, s, a, g_pg, g_raster);
}

void SecondFrame(Context& ctx, AppServices& s, AppActions& a) {
    const EditorResult e = BuildThemeEditor(ctx, s.layout, g_editor, s.themes, s.themeIndex,
                                            chrome::CONTENT_TOP);
    if (e.select_theme >= 0) a.theme = e.select_theme;
    if (e.close) a.close_second = true;
}

} // namespace app
