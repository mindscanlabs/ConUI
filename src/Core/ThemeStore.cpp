#include "ThemeStore.hpp"
#include "Json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string lower(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::tolower((unsigned char)c);
    return o;
}

bool iequal(const std::string& a, const char* b) {
    return b && lower(a) == lower(std::string(b));
}

/** A file name that is safe on both platforms and recognisable to a human. */
std::string fileNameFor(const Theme& t) {
    std::string s = lower(t.name);
    for (char& c : s)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '-';
    if (s.empty()) s = "theme";
    return s + ".json";
}

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

const Theme& ThemeStore::at(int i) const {
    if (i < 0 || i >= (int)themes_.size()) return themes_.front();
    return themes_[(size_t)i];
}

int ThemeStore::indexOf(const char* name) const {
    for (size_t i = 0; i < themes_.size(); ++i)
        if (iequal(themes_[i].name, name)) return (int)i;
    return -1;
}

void ThemeStore::loadBuiltins() {
    themes_.clear();
    for (int i = 0; i < BuiltinThemeCount(); ++i) themes_.push_back(BuiltinTheme(i));
}

void ThemeStore::init(const std::string& rootDir) {
    warnings_.clear();
    loadBuiltins();
    if (rootDir.empty()) { saved_ = themes_; return; }   // built-ins only; the tests use this

    dir_ = rootDir + "themes/";
    writeMissingBuiltins();
    scanDirectory();
    saved_ = themes_;                     // what the editor's REVERT goes back to
}

void ThemeStore::writeMissingBuiltins() {
    std::error_code ec;
    if (!fs::exists(dir_, ec)) {
        fs::create_directories(dir_, ec);
        if (ec) {
            warnings_.push_back("cannot create " + dir_ + ": " + ec.message());
            return;
        }
    }
    // Only files that are absent get written. An edited Phosphor must survive
    // a restart, so this never overwrites.
    for (int i = 0; i < BuiltinThemeCount(); ++i) {
        const Theme& t = BuiltinTheme(i);
        const fs::path p = fs::path(dir_) / fileNameFor(t);
        if (fs::exists(p, ec)) continue;
        std::ofstream f(p, std::ios::binary);
        if (!f) { warnings_.push_back("cannot write " + p.string()); continue; }
        const std::string text = writeTheme(t);
        f.write(text.data(), (std::streamsize)text.size());
    }
}

void ThemeStore::scanDirectory() {
    std::error_code ec;
    if (!fs::is_directory(dir_, ec)) return;

    // Sorted, so the menu order does not depend on the filesystem's mood.
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        if (lower(e.path().extension().string()) != ".json") continue;
        files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());

    for (const fs::path& p : files) {
        std::string src;
        if (!readFile(p, src)) {
            warnings_.push_back(p.filename().string() + ": cannot read");
            continue;
        }

        // The fallback for missing slots is the built-in of the same name if
        // there is one, otherwise Phosphor (§15.4). Work that out before
        // parsing, from the file's own "name" field.
        json::Value probe;
        json::Error perr;
        if (!json::Parse(src, probe, perr)) {
            // A broken file is skipped, never fatal — with the line number,
            // because "invalid JSON" alone helps nobody.
            warnings_.push_back(p.filename().string() + ": " + perr.where());
            continue;
        }
        std::string name;
        probe.getString("name", name);

        const Theme* fallback = &themes_.front();
        for (int i = 0; i < BuiltinThemeCount(); ++i)
            if (iequal(BuiltinTheme(i).name, name.c_str())) fallback = &BuiltinTheme(i);

        Theme t;
        std::string err;
        if (!parseTheme(src, *fallback, t, err)) {
            warnings_.push_back(p.filename().string() + ": " + err);
            continue;
        }
        t.source_path = p.string();
        t.built_in = false;

        const int existing = indexOf(t.name.c_str());
        if (existing >= 0) themes_[(size_t)existing] = t;   // overrides the built-in
        else               themes_.push_back(t);
    }
}

bool ThemeStore::parseTheme(const std::string& src, const Theme& fallback,
                            Theme& out, std::string& err) {
    json::Value doc;
    json::Error jerr;
    if (!json::Parse(src, doc, jerr)) { err = jerr.where(); return false; }
    if (!doc.isObject()) { err = "top level is not an object"; return false; }

    Theme t = fallback;                 // every slot starts at the fallback
    t.built_in = false;
    t.source_path.clear();

    // The name has to come from the DOCUMENT, not from the fallback the theme
    // was seeded with — otherwise a file with no name at all inherits one and
    // the check below can never fire.
    const json::Value* nameV = doc.get("name");
    if (!nameV || !nameV->isString() || nameV->string.empty()) {
        err = "\"name\" is missing, empty or not a string";
        return false;
    }
    t.name = nameV->string;
    doc.getString("author", t.author);

    const json::Value* colors = doc.get("colors");
    if (!colors || !colors->isObject()) { err = "\"colors\" is missing or not an object"; return false; }

    for (const auto& m : colors->members) {
        const int slot = SlotFromName(m.first.c_str());
        if (slot < 0) {
            // An unknown key is a warning, not a failure: a theme written for
            // a later version must still load in this one (§15).
            continue;
        }
        if (!m.second.isString()) { err = "\"" + m.first + "\" is not a string"; return false; }
        Rgba c;
        if (!ParseHexColor(m.second.string.c_str(), c)) {
            err = "\"" + m.first + "\": " + m.second.string + " is not #RRGGBB or #RRGGBBAA";
            return false;
        }
        t.c[slot] = c;
    }

    out = t;
    return true;
}

std::string ThemeStore::writeTheme(const Theme& t) {
    json::Members colors;
    colors.reserve(SLOT_COUNT);
    for (int s = 0; s < SLOT_COUNT; ++s)
        colors.emplace_back(kSlotNames[s], json::Str(FormatHexColor(t.c[s])));

    json::Members root;
    root.emplace_back("name",    json::Str(t.name));
    root.emplace_back("author",  json::Str(t.author.empty() ? "ConUI" : t.author));
    root.emplace_back("version", json::Num(1));
    root.emplace_back("colors",  json::Obj(std::move(colors)));

    return json::Write(json::Obj(std::move(root)));
}

bool ThemeStore::save(const Theme& t, std::string& err) const {
    if (dir_.empty()) { err = "no themes directory"; return false; }
    std::error_code ec;
    fs::create_directories(dir_, ec);

    const fs::path p = t.source_path.empty() ? (fs::path(dir_) / fileNameFor(t))
                                             : fs::path(t.source_path);
    std::ofstream f(p, std::ios::binary);
    if (!f) { err = "cannot write " + p.string(); return false; }
    const std::string text = writeTheme(t);
    f.write(text.data(), (std::streamsize)text.size());
    if (!f.good()) { err = "write failed: " + p.string(); return false; }
    return true;
}

// ---- live editing ------------------------------------------------------------

Theme& ThemeStore::edit(int i) {
    if (i < 0 || i >= (int)themes_.size()) return themes_.front();
    return themes_[(size_t)i];
}

int ThemeStore::changedSlots(int i) const {
    if (i < 0 || i >= (int)themes_.size() || i >= (int)saved_.size()) return 0;
    int n = 0;
    for (int s = 0; s < SLOT_COUNT; ++s) {
        const Rgba& a = themes_[(size_t)i].c[s];
        const Rgba& b = saved_[(size_t)i].c[s];
        if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) ++n;
    }
    // A duplicate that was never written is modified as a whole, even if its
    // colours still match the theme it was copied from.
    if (n == 0 && themes_[(size_t)i].source_path.empty() && !themes_[(size_t)i].built_in &&
        saved_[(size_t)i].name != themes_[(size_t)i].name)
        n = SLOT_COUNT;
    return n;
}

void ThemeStore::revert(int i) {
    if (i < 0 || i >= (int)themes_.size() || i >= (int)saved_.size()) return;
    for (int s = 0; s < SLOT_COUNT; ++s) themes_[(size_t)i].c[s] = saved_[(size_t)i].c[s];
}

std::string ThemeStore::pathOf(int i) const {
    const Theme& t = at(i);
    if (!t.source_path.empty()) return t.source_path;
    return dir_.empty() ? std::string("(built-in)") : (fs::path(dir_) / fileNameFor(t)).string();
}

bool ThemeStore::saveAt(int i, std::string& err) {
    if (i < 0 || i >= (int)themes_.size()) { err = "no such theme"; return false; }
    Theme& t = themes_[(size_t)i];
    if (!save(t, err)) return false;
    if (t.source_path.empty()) t.source_path = pathOf(i);
    if ((int)saved_.size() <= i) saved_.resize((size_t)i + 1);
    saved_[(size_t)i] = t;
    return true;
}

int ThemeStore::duplicate(int i) {
    Theme copy = at(i);
    copy.built_in = false;
    copy.source_path.clear();
    const std::string base = copy.name;
    for (int k = 2; k < 1000; ++k) {
        copy.name = base + " " + std::to_string(k);
        if (indexOf(copy.name.c_str()) < 0) break;
    }
    themes_.push_back(copy);
    // The "saved" twin keeps the ORIGINAL name, so the copy reads as
    // modified until it is written for the first time.
    Theme twin = copy;
    twin.name = base;
    saved_.push_back(twin);
    return (int)themes_.size() - 1;
}
