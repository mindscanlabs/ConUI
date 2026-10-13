#include "IconSheets.hpp"
#include "RasterStore.hpp"
#include "ResourceManager.hpp"

#include <map>
#include <tuple>

namespace {

// Keyed by the store too: the self-test builds throwaway stores of its own,
// and an id is only meaningful in the store it was added to.
std::map<std::tuple<const RasterStore*, std::string, int, int>, IconStrip> g_cache;

} // namespace

const IconStrip* LoadIconStrip(RasterStore& store, const std::string& prefix, int px, int count) {
    if (px <= 0 || count <= 0) return nullptr;
    const auto key = std::make_tuple((const RasterStore*)&store, prefix, px, count);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second.valid() ? &it->second : nullptr;

    IconStrip strip;
    std::vector<int> ids((size_t)count, -1);
    for (int d = 0; d <= 16 && !strip.valid(); ++d) {
        for (int s : { px - d, px + d }) {
            if (s <= 0) continue;
            size_t n = 0;
            const void* p = ResourceManager::Get().GetResource(prefix + "_" + std::to_string(s) + ".png", n);
            if (!p || !n) continue;
            const int got = store.addStripFromMemory((const uint8_t*)p, n, count, ids.data());
            if (got > 0) { strip.ids = ids; strip.px = got; break; }
        }
    }
    IconStrip& slot = g_cache[key];       // a miss is cached too: no retry every frame
    slot = strip;
    return slot.valid() ? &slot : nullptr;
}
