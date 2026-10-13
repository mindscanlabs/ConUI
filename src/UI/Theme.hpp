#pragma once
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// The palette: 32 slots, exactly as listed in docs/DESIGN_SYSTEM.md §5.
// Cells store a slot index, never a colour, so a theme swap costs nothing and
// a swatch can paint itself by using its own slot as fg (§8.16).
//
// Themes are loaded from themes/*.json; the three built-ins are compiled in so
// the app always has something to draw with (§15).
// ---------------------------------------------------------------------------

enum Slot : uint8_t {
    BG_WINDOW = 0, BG_PANEL, BG_RAISED, BG_SUNKEN, BG_SEL,
    BORDER_DIM, BORDER, BORDER_FOCUS,
    TEXT, TEXT2, TEXT_MUTED, TEXT_INV,
    ACCENT, ACCENT_DIM, WARN, DANGER, SUCCESS, INFO, SHADOW, DITHER,
    RETRO_ON, RETRO_OFF,          // the retro bar's ■ and □ (§8.3)
    SIDEBAR_BG, SIDEBAR_ICON, SIDEBAR_HOVER, SIDEBAR_ACTIVE,   // the sidebar (§8.18)
    TABLE_COLSEP, TABLE_ROWSEP,   // tables: │ between columns, the 1 px line between rows (§8.8, §8.19)
    CHART_1, CHART_2, CHART_3, CHART_4,   // the charts' series, in order (§8.20)
    SLOT_COUNT
};

/** Slot names as they appear as JSON keys. Indexed by Slot. */
extern const char* const kSlotNames[SLOT_COUNT];

/** Returns the slot for a JSON key, or -1 if the key is unknown. */
int SlotFromName(const char* name);

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct Theme {
    std::string name;
    std::string author;
    std::string source_path;  // empty for a built-in that has no file yet
    bool  built_in = false;
    Rgba  c[SLOT_COUNT]{};

    const Rgba& operator[](Slot s) const { return c[s]; }
};

/** Parses "#RRGGBB" or "#RRGGBBAA". Returns false on anything else. */
bool ParseHexColor(const char* s, Rgba& out);
/** Formats as "#RRGGBB", or "#RRGGBBAA" when alpha is not 255. */
std::string FormatHexColor(const Rgba& c);

/** The three built-in themes, in menu order. Phosphor is the default. */
int BuiltinThemeCount();
const Theme& BuiltinTheme(int index);
