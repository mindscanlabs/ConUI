#pragma once
#include <string>

class ThemeStore;
namespace ui { class Context; class Layout; }

// ---------------------------------------------------------------------------
// The theme editor (docs/DESIGN_SYSTEM.md §8.16) — the whole body of the
// second OS window. Built from the ordinary widgets and nothing else: the
// slot list, a Split, four Sliders, a TextInput for the hex, a Dropdown for
// the theme, and a live preview drawn with the very palette being edited.
//
// Edits go straight into the ThemeStore, so the main window changes colour on
// the same frame as the preview here.
// ---------------------------------------------------------------------------

/** What the editor remembers between frames. The caller owns it. */
struct ThemeEditorState {
    int  slot = 1;               // the slot being edited — bg.panel to start with
    std::string hex;             // the HEX field's text
    std::string message;         // the last save's outcome, for the status line
    bool message_error = false;
    int  list_scroll = 0;
};

/** What the editor asks the app to do after the frame. */
struct EditorResult {
    int  select_theme = -1;      // the THEME dropdown or NEW picked another theme
    bool close = false;          // Esc
};

/**
 * @param top         first row below the caption (chrome::CONTENT_TOP)
 * @param themeIndex  the theme both windows are drawn with — the one edited
 */
EditorResult BuildThemeEditor(ui::Context& ctx, ui::Layout& lay, ThemeEditorState& st,
                              ThemeStore& themes, int themeIndex, int top);
