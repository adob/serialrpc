#pragma once

#include <cstdint>

namespace serialrpc {
    struct MethodInfo {
        const char* name;
        uint32_t id;
    };
}
