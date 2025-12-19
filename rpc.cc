#include "rpc.h"
#include "lib/fmt/fmt.h"

using namespace lib;

namespace serialrpc {


void ErrReply::fmt(io::Writer &out, error) const {
  ErrReply const &e = *this;
  fmt::fprintf(out, "%s::%s rpc replied with error: %v", 
      e.service_name,
      e.procedure_name,
      e.msg);
}

} // namespace serialrpc
