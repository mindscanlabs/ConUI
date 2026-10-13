// ---------------------------------------------------------------------------
// The fork template: an empty application on the ConUI core.
//
// Build it with   cmake -DCONUI_APP=skeleton ..
// Start a fork by copying this file — docs/FORK.md walks through the rest.
//
// What you get for free, without writing a line of it: a native window with
// the System 7 caption, the theme files and the theme store, the locale
// layer, data.bin (theme, locale, opacity, density, split fractions), the
// immediate-mode widgets, one draw call per frame. What is left to you is
// the body of the window — MainFrame() below.
// ---------------------------------------------------------------------------

#include "App.hpp"

#include "../Core/ThemeStore.hpp"
#include "../Core/Utils.hpp"
#include "../Render/CellBuffer.hpp"
#include "../UI/Context.hpp"
#include "../UI/Glyphs.hpp"
#include "../UI/Layout.hpp"
#include "../UI/Strings.hpp"
#include "../UI/Theme.hpp"
#include "../UI/WindowChrome.hpp"
#include "../UI/Widgets/Widgets.hpp"

#include <cstdio>

using namespace ui;

namespace app {

// Your application's name in the caption, and its first window size in cells.
const char* Title() { return "CONUI · SKELETON"; }
void MainSize(int& cols, int& rows) { cols = 96; rows = 30; }

// A single-window application: no second window. Return a title here and
// fill SecondFrame() to get one (the playground's theme editor is an example).
const char* SecondTitle() { return nullptr; }
void SecondSize(int& cols, int& rows) { cols = 0; rows = 0; }

// Once, when the window and the stores exist: load pictures, open files.
void Init(AppServices&) {}

// One frame of the window body. Everything is immediate mode: draw what the
// screen should show NOW, and read the widgets' return values for what the
// user did. Row 0 is the caption (drawn by the core), row 1 stays clear.
void MainFrame(Context& ctx, AppServices& s, AppActions& a) {
    CellBuffer& cb = ctx.cells();
    const int W = cb.width(), H = cb.height();
    // No caption in fullscreen, so nothing reserves its rows either.
    const int top = s.fullscreen ? 0 : chrome::CONTENT_TOP;
    if (W < 30 || H < top + 8) return;

    // ---- the body: one panel. Replace this with your application. -------
    const Rect body{ 1, top, W - 2, H - 3 - top + 1 };
    BeginPanel(ctx, "main", str::T(str::ID_APP_TITLE), body);
    Label(ctx, "Start here: src/App/Skeleton.cpp, MainFrame().", TEXT);
    ctx.newline(2);
    if (Button(ctx, str::ID_LOCALE)) {
        const int next = (str::GetLocale() + 1) % str::LOCALE_COUNT;
        str::SetLocale(next);
        s.state.locale = str::LocaleCode(next);
    }
    ctx.spacer(1);
    if (Button(ctx, str::ID_CANCEL)) a.quit = true;
    EndPanel(ctx);

    // ---- the status bar, under an edge-to-edge rule ----------------------
    chrome::DrawRule(cb, H - 2, /*raised=*/true);
    char badge[32];
    std::snprintf(badge, sizeof badge, " %s ", str::T(str::ID_READY));
    cb.text(2, H - 1, badge, TEXT_INV, BG_SEL, ATTR_BOLD);
    cb.textRight(W - 3, H - 1, s.themes.at(s.themeIndex).name.c_str(), TEXT_MUTED, BG_WINDOW);
}

void SecondFrame(Context&, AppServices&, AppActions&) {}

// Your own state for data.bin: return any string, get it back next run. The
// playground uses JSON (src/Core/Json.hpp) — tolerant of old and new files.
void LoadState(const std::string&) {}
std::string SaveState() { return {}; }

} // namespace app
