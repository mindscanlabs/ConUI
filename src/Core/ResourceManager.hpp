#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

class ResourceManager {
public:
    static ResourceManager& Get() {
        static ResourceManager instance;
        return instance;
    }

    bool Load(const std::string& path);
    const void* GetResource(const std::string& name, size_t& out_size);

private:
    ResourceManager() = default;
    
    struct ResourceInfo {
        uint32_t offset;
        uint32_t size;
    };

    std::string packed_file_path;
    std::map<std::string, ResourceInfo> directory;
    std::map<std::string, std::vector<char>> cache;
};
