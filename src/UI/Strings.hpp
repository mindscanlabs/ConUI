#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// The localisation layer (docs/DESIGN_SYSTEM.md §14).
//
// It goes in BEFORE the widgets, not after: retrofitting T() once the literals
// have scattered across fourteen files costs far more than writing it now.
//
// English is the default; Russian ships as the second locale so that switching
// is a working demonstration rather than a promise. No gettext, no .po, no
// external format — an enum and a table per locale.
//
// The grid constraint is real and is part of the data: Russian is markedly
// longer than English (RUN 3 -> ЗАПУСК 6), and a translation that overflows
// its column breaks the layout rather than merely looking untidy. Every string
// therefore carries a maximum width IN CELLS, and the self-test fails the
// build if any translation exceeds it.
// ---------------------------------------------------------------------------

namespace str {

enum Id : int {
    // window and chrome
    ID_APP_TITLE = 0,
    ID_THEME_EDITOR,
    ID_MENU_FILE,
    ID_MENU_VIEW,
    ID_MENU_WIDGETS,
    ID_MENU_THEME,
    ID_MENU_HELP,
    // actions
    ID_RUN,
    ID_STOP,
    ID_RESET,
    ID_SAVE,
    ID_OPEN,
    ID_CANCEL,
    ID_DELETE,
    ID_FAULT,
    // labels
    ID_OPACITY,
    ID_PANEL_OPACITY,
    ID_BUILD,
    ID_PREVIEW,
    ID_LOCALE,
    ID_READY,
    // widgets and the widget demo
    ID_ON,
    ID_OFF,
    ID_BLUR,
    ID_DENSITY,
    ID_DENS_COMPACT,
    ID_DENS_NORMAL,
    ID_DENS_LARGE,
    ID_OPT_TRANSPARENT,
    ID_OPT_GRID,
    ID_OPT_AUTOSAVE,
    ID_OPT_GLITCH,
    ID_RANGE,
    ID_STEPS,
    ID_TESTS,
    ID_PENDING,
    ID_ROWS,
    ID_NAME,
    ID_SEARCH,
    ID_PORT,
    ID_PH_SEARCH,
    ID_NOTES,
    ID_PROCESSES,
    ID_FILESYSTEM,
    ID_COL_PID,
    ID_COL_CPU,
    ID_COL_MEM,
    ID_COL_STATE,
    ID_LN_COL,
    ID_N_LINES,
    ID_OVR,
    ID_TAB_INPUT,
    ID_CONFIRM,
    ID_CONFIRM_Q,
    ID_CONFIRM_NOTE,
    ID_DELETE_DOTS,
    ID_M_OPEN,
    ID_M_RENAME,
    ID_M_COPY_PATH,
    ID_M_SHOW_HIDDEN,
    ID_M_SORT_BY,
    ID_M_BY_NAME,
    ID_M_BY_SIZE,
    ID_M_BY_DATE,
    ID_M_DELETE,
    ID_TIP_OPACITY,
    ID_TIP_PANEL_OPACITY,
    ID_ED_NEW,
    ID_ED_REVERT,
    ID_ED_FILE,
    ID_ED_MODIFIED,
    ID_ED_SAVED,
    ID_ED_SLOT,
    ID_ED_PREVIEW,
    ID_ED_SLOTS,
    ID_ED_CHANGED,
    ID_ED_HINT,
    ID_ED_PV_SEL,
    ID_ED_PV_DITHER,
    ID_ED_PV_DANGER,
    ID_ED_OPEN_EDITOR,
    ID_VIEW_WORKSPACE,
    ID_VIEW_STATES,
    ID_VIEW_GLYPHS,
    ID_M_FULLSCREEN,
    ID_M_QUIT,
    ID_M_ABOUT,
    ID_M_THEME_EDITOR,
    ID_M_TAB_CONTROLS,
    ID_M_TAB_INPUT,
    ID_M_TAB_SETTINGS,
    ID_TAB_SETTINGS,
    ID_COMPILE,
    ID_RESOURCES,
    ID_LINK,
    ID_STATES,
    ID_ST_WIDGET,
    ID_ST_IDLE,
    ID_ST_HOVER,
    ID_ST_ACTIVE,
    ID_ST_FOCUS,
    ID_ST_DISABLED,
    ID_ST_ERROR,
    ID_W_BUTTON,
    ID_W_DEFAULT,
    ID_W_DANGER,
    ID_W_CHECKBOX,
    ID_W_RADIO,
    ID_W_SWITCH,
    ID_W_SLIDER,
    ID_W_TEXT,
    ID_W_DROPDOWN,
    ID_VOCAB,
    ID_SOLID,
    ID_SHADES,
    ID_HINT_F1,
    ID_HINT_QUITKEY,
    ID_FMT_METRICS,
    ID_ABOUT_TITLE,
    ID_ABOUT_1,
    ID_ABOUT_2,
    ID_ABOUT_3,
    ID_OK,
    // hints
    ID_HINT_FULLSCREEN,
    // the editable table (§8.19)
    ID_ET_INSERT_ROW,
    ID_ET_DELETE_ROW,
    ID_TASKS,
    ID_COL_TASK,
    ID_COL_OWNER,
    ID_COL_NOTES,
    ID_DENS_TINY,
    // the charts screen (§8.20)
    ID_VIEW_CHARTS,
    ID_CH_TRAFFIC,
    ID_CH_CPU,
    ID_CH_WEEKLY,
    ID_CH_DISK,
    // the diagrams screen (§8.21)
    ID_VIEW_DIAGRAMS,
    ID_DIAG_HINT,
    ID_DG_TREE,
    ID_DG_NET,
    ID_COUNT
};

enum Locale : int { LOCALE_EN = 0, LOCALE_RU = 1, LOCALE_COUNT };

/** Switches locale at runtime. Out-of-range values are ignored. */
void SetLocale(int locale);
int  GetLocale();
/** "en" / "ru", and the reverse. */
const char* LocaleCode(int locale);
int  LocaleFromCode(const char* code);
/** Name of the locale in its own language, for the menu. */
const char* LocaleName(int locale);

/**
 * The string for the current locale. A translation that is missing or empty
 * falls back to English, so a half-finished locale degrades instead of
 * showing blanks.
 */
const char* T(Id id);
/** The same, for an explicit locale — the width check needs it. */
const char* T(Id id, int locale);

/** Declared maximum width in cells. 0 means "unconstrained". */
int MaxWidth(Id id);

/** Stable key, for diagnostics. */
const char* Key(Id id);

} // namespace str
