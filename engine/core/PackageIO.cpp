#include "PackageIO.h"

#include <fstream>

namespace Demon::PackageIO {

namespace {

std::filesystem::path executableDirectory()
{
#ifdef DEMON_PLATFORM_WINDOWS
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    return {};
}

std::vector<uint8_t> readArchiveEntry(const std::filesystem::path& archivePath,
                                      const std::string& requestedEntry,
                                      std::string_view expectedMagic)
{
    std::ifstream file(archivePath, std::ios::binary);
    if (!file.is_open())
        return {};

    std::array<char, 8> magic{};
    file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!file.good() || std::string_view(magic.data(), magic.size()) != expectedMagic)
        return {};

    uint32_t version = 0;
    uint32_t entryCount = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&entryCount), sizeof(entryCount));
    if (!file.good() || version != 1)
        return {};

    for (uint32_t i = 0; i < entryCount; ++i) {
        uint32_t pathLength = 0;
        file.read(reinterpret_cast<char*>(&pathLength), sizeof(pathLength));
        if (!file.good() || pathLength > 64 * 1024)
            return {};

        std::string entryPath(pathLength, '\0');
        if (pathLength > 0)
            file.read(entryPath.data(), static_cast<std::streamsize>(entryPath.size()));

        uint64_t byteCount = 0;
        int64_t writeTimeTicks = 0;
        file.read(reinterpret_cast<char*>(&byteCount), sizeof(byteCount));
        file.read(reinterpret_cast<char*>(&writeTimeTicks), sizeof(writeTimeTicks));
        if (!file.good())
            return {};

        const bool matches = entryPath == requestedEntry
            || std::filesystem::path(entryPath).filename().string() == requestedEntry;
        if (matches) {
            std::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            if (byteCount > 0)
                file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.good() || file.eof() ? bytes : std::vector<uint8_t>{};
        }

        file.seekg(static_cast<std::streamoff>(byteCount), std::ios::cur);
        if (!file.good())
            return {};
    }

    return {};
}

std::vector<uint8_t> readFirstArchiveEntry(const std::filesystem::path& folder,
                                           const std::string& extension,
                                           const std::string& requestedEntry,
                                           std::string_view expectedMagic)
{
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec))
        return {};

    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec || !entry.is_regular_file() || entry.path().extension() != extension)
            continue;
        if (auto bytes = readArchiveEntry(entry.path(), requestedEntry, expectedMagic); !bytes.empty())
            return bytes;
    }
    return {};
}

} // namespace

std::vector<uint8_t> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return {};

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0)
        return {};
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file.good() || file.eof() ? bytes : std::vector<uint8_t>{};
}

std::vector<uint8_t> readRuntimeBinary(const std::filesystem::path& path)
{
    if (auto bytes = readBinaryFile(path); !bytes.empty())
        return bytes;

    const std::filesystem::path executableDir = executableDirectory();
    if (!path.is_absolute() && !executableDir.empty()) {
        if (auto bytes = readBinaryFile(executableDir / path); !bytes.empty())
            return bytes;
    }

    if (path.extension() == ".cso") {
        const std::string shaderName = path.filename().string();
        if (auto bytes = readFirstArchiveEntry("Data/visuals", ".dcs", shaderName, "DMONDCS1"); !bytes.empty())
            return bytes;
        if (!executableDir.empty())
            return readFirstArchiveEntry(executableDir / "Data" / "visuals", ".dcs", shaderName, "DMONDCS1");
    }

    return {};
}

} // namespace Demon::PackageIO
