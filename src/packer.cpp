#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <cstdint>

struct ResourceEntry {
    std::string name;
    uint32_t offset;
    uint32_t size;
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: packer <output_file> <input_file1> [input_file2] ..." << std::endl;
        return 1;
    }

    std::string output_path = argv[1];
    std::vector<std::string> input_files;
    for (int i = 2; i < argc; ++i) {
        input_files.push_back(argv[i]);
    }

    std::vector<ResourceEntry> entries;
    std::vector<std::vector<char>> data_blocks;

    // First pass: Read all files and prepare directory
    for (const auto& file_path : input_files) {
        std::ifstream in(file_path, std::ios::binary | std::ios::ate);
        if (!in.is_open()) {
            std::cerr << "Failed to open input file: " << file_path << std::endl;
            continue;
        }

        uint32_t size = static_cast<uint32_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
        if (in.read(buffer.data(), size)) {
            // Get filename from path
            std::string name = file_path;
            size_t last_slash = name.find_last_of("\\/");
            if (last_slash != std::string::npos) {
                name = name.substr(last_slash + 1);
            }

            entries.push_back({name, 0, size}); // Offset will be calculated after header
            data_blocks.push_back(std::move(buffer));
        }
    }

    // Calculate offsets
    // Magic (4) + Count (4) + Entries (NameSize(1) + Name(max 255) + Offset(4) + Size(4))
    // Header first, then the directory, then the data.
    
    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << output_path << std::endl;
        return 1;
    }

    // Magic number
    uint32_t magic = 0x5250414B; // "RPAK"
    out.write(reinterpret_cast<const char*>(&magic), 4);

    // Count
    uint32_t count = static_cast<uint32_t>(entries.size());
    out.write(reinterpret_cast<const char*>(&count), 4);

    // Placeholder for the directory (rewritten later with the correct offsets)
    auto directory_pos = out.tellp();
    for (const auto& entry : entries) {
        uint8_t name_len = static_cast<uint8_t>(entry.name.size());
        out.write(reinterpret_cast<const char*>(&name_len), 1);
        out.write(entry.name.c_str(), name_len);
        uint32_t dummy = 0;
        out.write(reinterpret_cast<const char*>(&dummy), 4); // offset
        out.write(reinterpret_cast<const char*>(&dummy), 4); // size
    }

    // Write data and record real offsets
    for (size_t i = 0; i < entries.size(); ++i) {
        entries[i].offset = static_cast<uint32_t>(out.tellp());
        out.write(data_blocks[i].data(), data_blocks[i].size());
    }

    // Go back and rewrite directory with real offsets
    out.seekp(directory_pos);
    for (const auto& entry : entries) {
        uint8_t name_len = static_cast<uint8_t>(entry.name.size());
        out.write(reinterpret_cast<const char*>(&name_len), 1);
        out.write(entry.name.c_str(), name_len);
        out.write(reinterpret_cast<const char*>(&entry.offset), 4);
        out.write(reinterpret_cast<const char*>(&entry.size), 4);
    }

    std::cout << "Successfully packed " << count << " files into " << output_path << std::endl;
    return 0;
}
