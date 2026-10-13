#pragma once
#include "../UI/Theme.hpp"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// themes/*.json: scan, load, override, save (docs/DESIGN_SYSTEM.md §15).
//
// The rules, in the order they matter:
//   1. the three built-ins are compiled in, so the app always has something
//      to draw with even on a blank disk;
//   2. if themes/ does not exist it is created and the built-ins are written
//      into it — there has to be something to open and edit;
//   3. themes/*.json is scanned; a file whose "name" matches a built-in
//      REPLACES it, so a user can retune Phosphor without forking the app;
//   4. a slot missing from a file falls back to the built-in of the same
//      name, else to Phosphor. A file with one colour in it is valid;
//   5. a broken file is SKIPPED with a message naming the file and the line.
//      One bad theme must never stop the program from starting.
// ---------------------------------------------------------------------------

class ThemeStore {
public:
    /**
     * @param rootDir directory holding themes/ (the exe dir, trailing slash).
     *                Empty means "built-ins only, touch no disk" — which is
     *                what the tests use.
     */
    void init(const std::string& rootDir);

    int          count() const { return (int)themes_.size(); }
    const Theme& at(int i) const;
    /** Index of a theme by name, case-insensitive; -1 when absent. */
    int          indexOf(const char* name) const;

    /** Writes a theme to themes/<file>.json. Returns false and fills `err`. */
    bool save(const Theme& t, std::string& err) const;

    // ---- live editing (the theme editor, §8.16) -------------------------
    //
    // The editor changes the theme IN the store, so every window that draws
    // with it shows the edit on the next frame — there is no copy to keep in
    // sync. What was last loaded or saved is kept beside it, and that is what
    // "modified", the changed-slot count and REVERT compare against.

    /** The theme, writable. Edits are live. */
    Theme& edit(int i);
    /** Slots that differ from the saved version. 0 = unmodified. */
    int  changedSlots(int i) const;
    bool modified(int i) const { return changedSlots(i) > 0; }
    /** Back to the saved version. */
    void revert(int i);
    /** Saves theme `i` and makes that the new saved version. */
    bool saveAt(int i, std::string& err);
    /**
     * A copy of theme `i` under a new, unused name ("Phosphor 2"), not yet on
     * disk. Returns its index.
     */
    int  duplicate(int i);
    /** Where theme `i` lives or will be written, for the editor's FILE line. */
    std::string pathOf(int i) const;

    /** Parses one theme document. Exposed so the tests can drive it directly. */
    static bool parseTheme(const std::string& src, const Theme& fallback,
                           Theme& out, std::string& err);
    /** Serialises a theme to the documented file format. */
    static std::string writeTheme(const Theme& t);

    /** Files that were skipped, and why. Shown in the theme editor's status. */
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    std::string             dir_;       // rootDir + "themes/"
    std::vector<Theme>      themes_;
    std::vector<Theme>      saved_;     // as last loaded or written, per index
    std::vector<std::string> warnings_;

    void loadBuiltins();
    void writeMissingBuiltins();
    void scanDirectory();
};
