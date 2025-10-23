#include "rpc.h"
#include "lib/fmt/fmt.h"

using namespace lib;

namespace serialrpc {


void ErrReply::fmt(io::Writer &out, error) const {
  fmt::fprintf(out, "rpc replied with error: %v", this->msg);
}

} // namespace serialrpc
