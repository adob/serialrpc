#pragma once

import <array>;
import <cstdint>;
import lib.str;
import lib.types;

namespace serialrpc {
    struct ServiceInfo {
        lib::str name;
        lib::str package;
        std::array<lib::uint8, 16> uuid;
        lib::int32 major_version;
        lib::int32 minor_version;
        lib::uint32 num_endpoints;
    };
}
