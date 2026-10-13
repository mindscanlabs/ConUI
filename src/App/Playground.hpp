#pragma once
#include "App.hpp"
#include "../UI/Widgets/Widgets.hpp"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The playground: the showcase screen, built from the
// real widgets, plus the screens that show every state and glyph:
//
//   Workspace  CONTROLS + BUILD │ FILESYSTEM │ PROCESSES + NOTES, a split tree
//   States     every widget in every state side by side — idle, hover,
//              active, focus, disabled, error — each drawing itself
//   Glyphs     the U+2500–259F sheet and the vocabulary (the old --chart)
//   Charts     lines and bars (§8.20)
//   Diagrams   a tree of boxes and a layered network, zoomed and moved (§8.21)
//
// A menu bar switches between them. Everything on it is live: the theme,
// the locale, the opacity, the OS material and the density are the app's
// real settings, not a picture of them.
// ---------------------------------------------------------------------------

/** Everything the playground's widgets edit. The caller owns every value. */
struct PlaygroundState {
    int  screen = 0;             // 0 workspace, 1 states, 2 glyphs, 3 charts, 4 diagrams
    int  tab = 0;                // CONTROLS: widgets / input / settings

    // WIDGETS
    bool  transparent = true;
    bool  grid = false;
    bool  blur = true;
    int   autosave = 2;          // mixed, so the third state is on show
    float steps = 3.0f;
    float range_lo = 12.0f, range_hi = 48.0f;

    // INPUT (§8.12)
    std::string name = "conui_playground";
    std::string search;
    std::string port = "80800";  // invalid on purpose: the error state is on show
    std::vector<std::string> history;

    // NOTES (§8.13)
    std::string notes;

    // PROCESSES (§8.8): the data, the order the caller sorted it into, and
    // the selection — the widget only reports clicks
    struct Proc { int pid; std::string name; float cpu; float mem; int state; };
    std::vector<Proc> procs;
    std::vector<int>  order;
    int  proc_sel = 1;
    int  sort_col = 2;
    bool sort_desc = true;

    // FILESYSTEM (§8.7) and its menu (§8.10)
    std::vector<ui::TreeNode> fs;
    const ui::TreeNode* fs_sel = nullptr;
    bool show_hidden = false;
    int  sort_by = 0;

    // TASKS (§8.19): editable cells; the rows belong to the playground, in the order it
    // sorted them into — the widget edits cells in place and reports the rest
    struct Task { int id; std::string cell[3]; };
    std::vector<Task> tasks;
    int  task_next_id = 0;
    int  task_row = 0, task_col = 0;
    int  task_sort = -1;
    bool task_desc = false;

    // CHARTS (§8.20): the live CPU chart's samples, oldest first, one every
    // half second; not saved
    std::vector<float> cpu_user, cpu_sys;
    int    cpu_tick = 0;          // samples taken so far: the next one's phase
    double cpu_next = 0.0;        // when the next sample is due

    // DIAGRAMS (§8.21): the network's weights, layer to layer; made once, not saved
    std::vector<float> net_w[3];

    // modals (§8.9)
    bool confirm = false;
    bool about = false;

    bool frozen = false;         // no animation: snapshots must not depend on the clock
    bool notes_loaded = false;   // NOTES came from data.bin: do not overwrite with the sample
    bool ready = false;          // the data above is filled on first use
};

/**
 * The playground's part of data.bin: the screen, the tab, what is typed in
 * the fields, the notes, the widget values, the table order. JSON, so an
 * older or newer file still loads — unknown keys are ignored, missing ones
 * keep their defaults.
 */
std::string SavePlaygroundState(const PlaygroundState& pg);
void LoadPlaygroundState(PlaygroundState& pg, const std::string& json);

/**
 * One frame of the playground's main window. `rasterId` is the preview
 * picture (MakeDemoRaster), or -1.
 */
void BuildPlayground(ui::Context& ctx, AppServices& s, AppActions& a,
                     PlaygroundState& pg, int rasterId);
