#include "ResourceManager.hpp"
#include <fstream>
#include <iostream>

bool ResourceManager::Load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    uint32_t magic = 0;
    in.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != 0x5250414B) return false;

    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&count), 4);

    directory.clear();
    packed_file_path = path;

    for (uint32_t i = 0; i < count; ++i) {
        uint8_t name_len = 0;
        in.read(reinterpret_cast<char*>(&name_len), 1);
        
        std::string name(name_len, '\0');
        in.read(&name[0], name_len);

        ResourceInfo info;
        in.read(reinterpret_cast<char*>(&info.offset), 4);
        in.read(reinterpret_cast<char*>(&info.size), 4);

        directory[name] = info;
    }

    return true;
}

const void* ResourceManager::GetResource(const std::string& name, size_t& out_size) {
    // Check cache first
    auto it_cache = cache.find(name);
    if (it_cache != cache.end()) {
        out_size = it_cache->second.size();
        return it_cache->second.data();
    }

    // Find in directory
    auto it_dir = directory.find(name);
    if (it_dir == directory.end()) return nullptr;

    // Load from disk to cache
    std::ifstream in(packed_file_path, std::ios::binary);
    if (!in.is_open()) return nullptr;

    in.seekg(it_dir->second.offset);
    std::vector<char> buffer(it_dir->second.size);
    if (in.read(buffer.data(), it_dir->second.size)) {
        out_size = buffer.size();
        cache[name] = std::move(buffer);
        return cache[name].data();
    }

    return nullptr;
}
