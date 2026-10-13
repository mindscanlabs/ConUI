#include "Utils.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>

float global_scale = 1.0f;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdlib.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

std::string GetExeDirectory() {
#ifdef _WIN32
    // The real app binary (ConUIApp.exe) runs from bin/ (see CMakeLists.txt /
    // src/launcher.cpp: Windows resolves an exe's directly linked DLLs from
    // its own directory before main() ever runs, so the runtime DLL closure
    // lives in its own bin/ subfolder instead of cluttering the top-level
    // folder next to resources.dat/data.bin/themes/). Everything that calls
    // GetExeDirectory() expects that top-level folder — the launcher sets this
    // env var to its own directory before spawning ConUIApp.exe. Falls back to
    // the exe's own directory when unset (e.g. ConUIApp.exe launched directly
    // for debugging).
    const char* app_root = std::getenv("CONUI_APP_ROOT");
    if (app_root && app_root[0] != '\0') {
        std::string s(app_root);
        if (!s.empty() && s.back() != '\\' && s.back() != '/') s += '\\';
        return s;
    }
    char path[MAX_PATH];
    if (GetModuleFileNameA(NULL, path, MAX_PATH) != 0) {
        std::string s(path);
        size_t last = s.find_last_of("\\/");
        if (last != std::string::npos) return s.substr(0, last + 1);
    }
#elif defined(__APPLE__)
    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char real_path[4096];
        if (realpath(path, real_path) != nullptr) {
            std::string p(real_path);
            size_t app_pos = p.find(".app/");
            if (app_pos != std::string::npos) {
                size_t slash = p.rfind('/', app_pos);
                if (slash != std::string::npos) return p.substr(0, slash + 1);
            } else {
                size_t slash = p.find_last_of('/');
                if (slash != std::string::npos) return p.substr(0, slash + 1);
            }
        }
    }
#elif defined(__linux__)
    // The binary's own folder, through the kernel's link to it; that is where
    // resources.dat and themes/ are staged (build_linux.sh, CMakeLists.txt).
    char path[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) {
        std::string p(path, (size_t)n);
        size_t slash = p.find_last_of('/');
        if (slash != std::string::npos) return p.substr(0, slash + 1);
    }
#endif
    return "";
}

namespace {

/** CONUI_APP_ROOT, if set, wins everywhere: the launcher and the tests use it. */
std::string appRootOverride() {
    const char* r = std::getenv("CONUI_APP_ROOT");
    if (!r || !*r) return "";
    std::string s(r);
    if (s.back() != '\\' && s.back() != '/') s += '/';
    return s;
}

#if defined(__APPLE__)
/** The bundle root ("…/ConUI.app/"), or "" when not running from a bundle. */
std::string bundleRoot() {
    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return "";
    char real_path[4096];
    if (realpath(path, real_path) == nullptr) return "";
    const std::string p(real_path);
    const size_t app = p.find(".app/");
    return app == std::string::npos ? std::string() : p.substr(0, app + 5);
}
#endif

} // namespace

std::string GetResourceDirectory() {
    const std::string over = appRootOverride();
    if (!over.empty()) return over;
#if defined(__APPLE__)
    const std::string b = bundleRoot();
    if (!b.empty()) return b + "Contents/Resources/";
#endif
    return GetExeDirectory();
}

std::string GetDataDirectory() {
    const std::string over = appRootOverride();
    if (!over.empty()) return over;
#if defined(__APPLE__)
    // Inside a bundle nothing may be written — a signed .app would break, and
    // /Applications is usually not writable at all. The user's Application
    // Support folder is where macOS expects this. UNVERIFIED on hardware.
    if (!bundleRoot().empty()) {
        const char* home = std::getenv("HOME");
        if (home && *home) {
            const std::string dir = std::string(home) + "/Library/Application Support/ConUI/";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (!ec) return dir;
        }
    }
#endif
    // Windows (and a bare binary elsewhere): portable — everything in one
    // folder, next to the launcher. Copy the folder, and the settings go too.
    return GetExeDirectory();
}

float FontPxForDensity(Density d) {
    switch (d) {
        case Density::Tiny:    return 11.0f;
        case Density::Compact: return 13.0f;
        case Density::Large:   return 18.0f;
        case Density::Normal:
        default:               return 15.0f;
    }
}

// ---------------------------------------------------------------------------
// data.bin — a single flat binary blob: magic + version, then every AppState
// field in a fixed order. When you add a field later, append it at the end of
// both serialize and deserialize and bump kVersion only if you also need to
// reject truly incompatible old files; a simple appended field can just
// default-initialize when reading a shorter, older buffer (see the
// `if (!r.ok) return true;` early-outs below).
// ---------------------------------------------------------------------------
namespace {
    constexpr uint32_t kMagic   = 0x49554E43; // "CNUI" little-endian
    constexpr uint32_t kVersion = 1;

    void write_str(std::vector<char>& buf, const std::string& s) {
        uint32_t len = (uint32_t)s.size();
        const char* p = reinterpret_cast<const char*>(&len);
        buf.insert(buf.end(), p, p + sizeof(len));
        buf.insert(buf.end(), s.begin(), s.end());
    }

    template <typename T>
    void write_val(std::vector<char>& buf, const T& val) {
        const char* p = reinterpret_cast<const char*>(&val);
        buf.insert(buf.end(), p, p + sizeof(val));
    }

    void write_geom(std::vector<char>& buf, const WindowGeometry& g) {
        write_val(buf, g.x); write_val(buf, g.y);
        write_val(buf, g.w); write_val(buf, g.h);
        write_val(buf, g.maximized);
    }

    struct Reader {
        const char* data;
        size_t size;
        size_t offset = 0;
        bool ok = true;

        template <typename T>
        bool val(T& out) {
            if (!ok || offset + sizeof(T) > size) { ok = false; return false; }
            std::memcpy(&out, data + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }
        bool str(std::string& out) {
            uint32_t len = 0;
            if (!val(len)) return false;
            if (offset + len > size) { ok = false; return false; }
            out.assign(data + offset, len);
            offset += len;
            return true;
        }
        bool geom(WindowGeometry& g) {
            return val(g.x) && val(g.y) && val(g.w) && val(g.h) && val(g.maximized);
        }
    };

    std::vector<char> serialize_app_state_impl(const AppState& s) {
        std::vector<char> buf;
        write_val(buf, kMagic);
        write_val(buf, kVersion);

        write_str(buf, s.theme);
        write_str(buf, s.locale);
        write_val(buf, s.opacity);
        write_val(buf, (int32_t)s.backdrop);
        write_val(buf, (int32_t)s.density);
        write_geom(buf, s.main_window);
        write_geom(buf, s.theme_editor);
        write_val(buf, s.theme_editor_open);
        write_val(buf, s.saved_gs);
        write_str(buf, s.layout);
        write_str(buf, s.app_data);
        write_val(buf, s.panel_opacity);
        write_val(buf, s.glitches);
        return buf;
    }

    bool deserialize_app_state_impl(const std::vector<char>& buf, AppState& s) {
        Reader r{buf.data(), buf.size()};
        uint32_t magic = 0, version = 0;
        if (!r.val(magic) || magic != kMagic) return false;
        if (!r.val(version) || version != kVersion) return false;

        // Every field from here on leaves its default in place if the file is
        // shorter than expected, so an older data.bin still loads.
        if (!r.str(s.theme))  return true;
        if (!r.str(s.locale)) return true;
        // Values are range-checked as they come in: a damaged or hand-edited
        // file must not hand the app a density of 7 or an opacity of NaN.
        if (!r.val(s.opacity)) return true;
        if (!(s.opacity >= 0.2f && s.opacity <= 1.0f)) s.opacity = AppState{}.opacity;
        int32_t backdrop = 0;
        if (!r.val(backdrop)) return true;
        if (backdrop >= (int32_t)Backdrop::None && backdrop <= (int32_t)Backdrop::Mica)
            s.backdrop = (Backdrop)backdrop;
        int32_t density = 0;
        if (!r.val(density)) return true;
        if (density >= (int32_t)Density::Compact && density <= (int32_t)Density::Tiny)
            s.density = (Density)density;
        if (!r.geom(s.main_window))  return true;
        if (!r.geom(s.theme_editor)) return true;
        if (!r.val(s.theme_editor_open)) return true;
        if (!r.val(s.saved_gs)) return true;
        if (!(s.saved_gs > 0.1f && s.saved_gs < 10.0f)) s.saved_gs = 1.0f;
        if (!r.str(s.layout)) return true;
        if (!r.str(s.app_data)) return true;
        if (!r.val(s.panel_opacity)) return true;
        if (!(s.panel_opacity >= 0.0f && s.panel_opacity <= 1.0f)) s.panel_opacity = 1.0f;
        if (!r.val(s.glitches)) return true;
        if (s.glitches > 1) s.glitches = 1;
        return true;
    }
}

std::vector<char> SerializeAppState(const AppState& state) {
    return serialize_app_state_impl(state);
}

bool DeserializeAppState(const std::vector<char>& buf, AppState& state) {
    return deserialize_app_state_impl(buf, state);
}

bool SaveAppState(const AppState& state) {
    const std::vector<char> buf = SerializeAppState(state);
    const std::string dir = GetDataDirectory();
    const std::string path = dir + "data.bin", tmp = dir + "data.bin.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        f.write(buf.data(), (std::streamsize)buf.size());
        f.flush();
        if (!f.good()) return false;
    }
    // Replace in one step: a crash or a full disk can lose THIS save, never
    // the previous one. std::filesystem::rename replaces an existing target.
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::filesystem::remove(tmp, ec); return false; }
    return true;
}

bool LoadAppState(AppState& state) {
    const std::string path = GetDataDirectory() + "data.bin";
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;

    std::streamsize size = f.tellg();
    if (size <= 0) return false;
    f.seekg(0, std::ios::beg);

    std::vector<char> buf((size_t)size);
    if (!f.read(buf.data(), size)) return false;

    return DeserializeAppState(buf, state);
}

void AdjustAppStateToScale(AppState& state, float current_gs) {
    if (state.saved_gs <= 0.01f || std::fabs(state.saved_gs - current_gs) < 0.001f) return;
    float factor = current_gs / state.saved_gs;
    auto rescale = [&](WindowGeometry& g) {
        if (g.x > 0) g.x *= factor;
        if (g.y > 0) g.y *= factor;
        if (g.w > 0) g.w *= factor;
        if (g.h > 0) g.h *= factor;
    };
    rescale(state.main_window);
    rescale(state.theme_editor);
    state.saved_gs = current_gs;
}
