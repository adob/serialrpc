#pragma once

import lib;

namespace serialrpc {
    using namespace lib;
    
    bool is_printable(uint8 c);
    String format_uuid(str uuid_bytes);
}
