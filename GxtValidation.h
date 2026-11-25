#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace gxt {

inline bool IsValidPcFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto length = static_cast<std::streamoff>(file.tellg());
    // The Hoodlum text loader stores file offsets as signed 32-bit integers.
    if (length < 12 || length > (std::numeric_limits<int32_t>::max)()) return false;

    std::vector<unsigned char> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) return false;
    if (bytes[0] != 4 || bytes[1] != 0 || bytes[2] != 8 || bytes[3] != 0) return false;

    const auto readOffset = [&](size_t position) {
        uint32_t value;
        std::memcpy(&value, bytes.data() + position, sizeof value);
        return value;
    };
    const auto chunk = [&](size_t position, const char* tag, size_t& size) {
        if (position > bytes.size() - 8 || std::memcmp(bytes.data() + position, tag, 4) != 0)
            return false;
        size = readOffset(position + 4);
        return size <= bytes.size() - position - 8;
    };
    const auto textTable = [&](size_t position) {
        size_t keySize = 0;
        if (!chunk(position, "TKEY", keySize) || keySize == 0 || keySize % 8 != 0
            || keySize / 8 > (std::numeric_limits<int16_t>::max)()) return false;
        const size_t keyStart = position + 8;
        const size_t dataHeader = keyStart + keySize;
        size_t dataSize = 0;
        if (!chunk(dataHeader, "TDAT", dataSize) || dataSize == 0) return false;
        // Every in-range string offset has a terminator within the TDAT chunk.
        if (bytes[dataHeader + 8 + dataSize - 1] != 0) return false;
        for (size_t key = keyStart; key < dataHeader; key += 8)
            if (readOffset(key) >= dataSize) return false;
        return true;
    };

    // A PC GXT can begin with the main table or with a mission directory.
    if (std::memcmp(bytes.data() + 4, "TABL", 4) != 0) return textTable(4);
    size_t directorySize = 0;
    if (!chunk(4, "TABL", directorySize) || directorySize / 12 > 200)
        return false;
    // Hoodlum consumes complete directory entries. Some existing PC files
    // count an extra partial entry in the declared TABL length.
    const size_t mainTable = 12 + (directorySize / 12) * 12;
    if (!textTable(mainTable)) return false;
    for (size_t entry = 12; entry < mainTable; entry += 12) {
        const size_t position = readOffset(entry + 8);
        if (std::memcmp(bytes.data() + entry, "MAIN\0\0\0\0", 8) == 0) {
            if (position != mainTable) return false;
            continue;
        }
        if (position < mainTable || position > bytes.size() - 8
            || std::memcmp(bytes.data() + position, bytes.data() + entry, 8) != 0
            || !textTable(position + 8)) return false;
    }
    return true;
}

}
