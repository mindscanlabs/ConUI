#include "Theme.hpp"
#include <cstdio>
#include <cstring>

const char* const kSlotNames[SLOT_COUNT] = {
    "bg.window", "bg.panel", "bg.raised", "bg.sunken", "bg.selection",
    "border.dim", "border", "border.focus",
    "text.primary", "text.secondary", "text.muted", "text.inverse",
    "accent", "accent.dim", "warn", "danger", "success", "info", "shadow", "dither",
    "retro.filled", "retro.empty",
    "sidebar.bg", "sidebar.icon", "sidebar.hover", "sidebar.active",
    "table.colsep", "table.rowsep",
    "chart.1", "chart.2", "chart.3", "chart.4",
};

int SlotFromName(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < SLOT_COUNT; ++i)
        if (std::strcmp(name, kSlotNames[i]) == 0) return i;
    return -1;
}

namespace {
    int hexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
}

bool ParseHexColor(const char* s, Rgba& out) {
    if (!s || *s != '#') return false;
    ++s;
    size_t n = std::strlen(s);
    if (n != 6 && n != 8) return false;
    int v[8];
    for (size_t i = 0; i < n; ++i) {
        v[i] = hexDigit(s[i]);
        if (v[i] < 0) return false;
    }
    out.r = (uint8_t)(v[0] * 16 + v[1]);
    out.g = (uint8_t)(v[2] * 16 + v[3]);
    out.b = (uint8_t)(v[4] * 16 + v[5]);
    out.a = (n == 8) ? (uint8_t)(v[6] * 16 + v[7]) : (uint8_t)255;
    return true;
}

std::string FormatHexColor(const Rgba& c) {
    char buf[12];
    if (c.a == 255) std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c.r, c.g, c.b);
    else            std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
    return buf;
}

// ---------------------------------------------------------------------------
// Built-ins. These must stay byte-identical to themes/*.json in the repo —
// docs/DESIGN_SYSTEM.md §5 lists the same values, and the round-trip test
// compares the two.
// ---------------------------------------------------------------------------
namespace {

struct Spec { const char* name; const char* hex[SLOT_COUNT]; };

const Spec kSpecs[] = {
    { "Phosphor", {
        "#062038","#0B3055","#124070","#03182A","#1FA35C",
        "#1B5585","#E8C14A","#FFE07A",
        "#EAF2FA","#9CC4E4","#5E87AC","#03182A",
        "#4AE8E8","#2A8C9C","#E8C14A","#FF6B6B","#7CE89A","#8AC8FF","#0000008C","#124070",
        "#2A8C9C","#5E87AC",
        "#124070","#5E87AC","#EAF2FA","#4AE8E8",
        "#1B5585","#1B5585",
        "#4AE8E8","#E8C14A","#7CE89A","#C49AFF" } },
    { "Paper", {
        "#DCDCDC","#E6E6E6","#F4F4F4","#C8C8C8","#0A0A0A",
        "#9A9A9A","#0A0A0A","#0A0A0A",
        "#0A0A0A","#3C3C3C","#8A8A8A","#F4F4F4",
        "#0A0A0A","#6E6E6E","#8A5A00","#B0122A","#0F6B2E","#123C8A","#000000B3","#9A9A9A",
        "#6E6E6E","#8A8A8A",
        "#F4F4F4","#8A8A8A","#0A0A0A","#0A0A0A",
        "#9A9A9A","#9A9A9A",
        "#0A0A0A","#6E6E6E","#3C3C3C","#A8A8A8" } },
    { "Midnight", {
        "#0B0E12","#11151B","#171D26","#070A0D","#3BE0C6",
        "#232B36","#384454","#3BE0C6",
        "#D7DEE8","#8A97A8","#545F6D","#06090C",
        "#3BE0C6","#1C8E7E","#E8A33D","#FF3DC8","#5BD97E","#5AA9FF","#0000008C","#1F2A36",
        "#1C8E7E","#545F6D",
        "#171D26","#545F6D","#D7DEE8","#3BE0C6",
        "#232B36","#232B36",
        "#3BE0C6","#5AA9FF","#E8A33D","#B48CFF" } },
};
constexpr int kSpecCount = (int)(sizeof kSpecs / sizeof *kSpecs);

Theme g_builtins[kSpecCount];
bool  g_built = false;

void build() {
    if (g_built) return;
    for (int i = 0; i < kSpecCount; ++i) {
        Theme& t = g_builtins[i];
        t.name = kSpecs[i].name;
        t.author = "ConUI";
        t.built_in = true;
        for (int s = 0; s < SLOT_COUNT; ++s)
            ParseHexColor(kSpecs[i].hex[s], t.c[s]);
    }
    g_built = true;
}

} // namespace

int BuiltinThemeCount() { build(); return kSpecCount; }

const Theme& BuiltinTheme(int index) {
    build();
    if (index < 0 || index >= kSpecCount) index = 0;
    return g_builtins[index];
}
