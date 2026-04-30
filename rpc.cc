#include "rpc.h"
#include "lib/fmt/fmt.h"

using namespace lib;

namespace serialrpc {

    void ErrReply::fmt(io::Writer &out, error) const {
        ErrReply const &e = *this;
        fmt::fprintf(out, "serialrpc on %q: %s::%s call returned error: %v", 
            e.io_name,
            e.service_name,
            e.procedure_name,
            e.msg);
    }

    void Err::fmt(io::Writer &out, error err) const {
        details->write_to(out, err);
    }
} // namespace serialrpc

