#pragma once
#include <string>
#include <vector>
#include <cstdint>

extern float global_scale;

// ---------------------------------------------------------------------------
// Persisted state (data.bin, next to the launcher exe). Add new fields at the
// END of AppState and at the END of serialize/deserialize_app_state() in
// Utils.cpp, following the same "read_val returns false -> leave the default"
// convention already used there — that keeps an old data.bin loadable after
// the struct grows, instead of forcing a version bump on every new field.
//
// The theme is stored by NAME only; the colours themselves live in
// themes/*.json (see docs/DESIGN_SYSTEM.md §15).
// ---------------------------------------------------------------------------

/** Cell size preset — see docs/DESIGN_SYSTEM.md §3 "Sizes and DPI". */
// Tiny came last and is appended, not inserted: data.bin stores the number.
enum class Density : int32_t { Compact = 0, Normal = 1, Large = 2, Tiny = 3 };

/**
 * OS backdrop material behind a translucent window (§6). Only `None` is
 * offered in the UI; the others stay defined so old `data.bin`/theme-editor
 * plumbing and the platform layer still compile against a stable enum, but
 * nothing in the app can select them any more.
 */
enum class Backdrop : int32_t { None = 0, Blur = 1, Acrylic = 2, Mica = 3 };

/** Geometry of one OS window, in pixels. -1 = "not positioned yet". */
struct WindowGeometry {
    float x = -1.0f, y = -1.0f, w = -1.0f, h = -1.0f;
    uint8_t maximized = 0;
};

struct AppState {
    std::string theme  = "Phosphor"; // name, resolved against themes/ at startup
    std::string locale = "en";

    float    opacity  = 0.97f;       // window background alpha, 0..1
    Backdrop backdrop = Backdrop::None;
    Density  density  = Density::Normal;

    WindowGeometry main_window;
    WindowGeometry theme_editor;
    uint8_t theme_editor_open = 1;   // the editor is on show from the first run

    float saved_gs = 1.0f; // global_scale at last save, for AdjustAppStateToScale()

    std::string layout;    // split fractions, ui::Layout::serialize() (§8.17)

    /**
     * The application's own state (App.hpp: SaveState/LoadState) — the
     * playground keeps its screen, tab and field contents here as JSON. The
     * core stores it and never looks inside.
     */
    std::string app_data;

    /**
     * Section (panel) background alpha, 0..1 — separate from `opacity`, which
     * is the window background's. Frames and text stay opaque either way.
     */
    float panel_opacity = 1.0f;

    /** Glitch effects on (1) or off (0) — every kind of them (§9). */
    uint8_t glitches = 1;
};

/**
 * Writes data.bin. Atomic: the bytes go to data.bin.tmp, which then replaces
 * data.bin, so a crash or a full disk mid-write leaves the previous file
 * intact rather than half of a new one. Returns false if nothing was written.
 */
bool SaveAppState(const AppState& state);
bool LoadAppState(AppState& state);
void AdjustAppStateToScale(AppState& state, float current_gs);

/** The data.bin encoding, exposed for the autosave's "did it change" and the tests. */
std::vector<char> SerializeAppState(const AppState& state);
/** False only for a file written by something else (wrong magic or version); short is fine. */
bool DeserializeAppState(const std::vector<char>& buf, AppState& state);

/**
 * Where the READ-ONLY files live: resources.dat. On Windows the launcher's
 * folder (the portable layout of dist/win); in a macOS bundle
 * Contents/Resources/.
 */
std::string GetResourceDirectory();
/**
 * Where the WRITABLE files live: data.bin and themes/. On Windows the same
 * folder as the resources — the app stays portable, one folder to copy. On
 * macOS ~/Library/Application Support/ConUI/, because a signed bundle must
 * not be written to (and /Applications usually cannot be).
 */
std::string GetDataDirectory();
/** The folder of the launcher (Windows) or of the .app (macOS). Trailing slash. */
std::string GetExeDirectory();

/** Font pixel size for a density preset, before global_scale is applied. */
float FontPxForDensity(Density d);
