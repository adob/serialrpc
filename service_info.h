#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace serialrpc {
    struct ServiceInfo {
        std::string_view Name;
        std::string_view Package;
        std::array<uint8_t, 16> UUID;
        int MajorVersion;
        int MinorVersion;
        int NumEndpoints;
    };
}
