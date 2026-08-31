#pragma once

#include "DemonPCH.h"

namespace Demon::PackageIO {

std::vector<uint8_t> readBinaryFile(const std::filesystem::path& path);
std::vector<uint8_t> readRuntimeBinary(const std::filesystem::path& path);

} // namespace Demon::PackageIO
