#pragma once

#include "core/DemonPCH.h"

namespace Demon {

template <size_t N>
inline void copyStringToBuffer(char (&buffer)[N], std::string_view text)
{
    static_assert(N > 0, "buffer must not be empty");

    const size_t copyLength = std::min(text.size(), N - 1);
    std::memcpy(buffer, text.data(), copyLength);
    buffer[copyLength] = '\0';

    if (copyLength + 1 < N)
        std::fill(buffer + copyLength + 1, buffer + N, '\0');
}

} // namespace Demon
