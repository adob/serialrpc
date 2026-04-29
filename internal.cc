#include "internal.h"

using namespace lib;

bool serialrpc::is_printable(uint8 c) {
    if (c >= ' '&& c <= '~') {
        return true;
    }
    
    return c == '\n' || c == '\r' || c == '\t' || c == '\v';
}

String serialrpc::format_uuid(str uuid_bytes) {
  String s;
  for (size i = 0; i < len(uuid_bytes); i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      s += "-";
    }
    s += fmt::sprintf("%02x", byte(uuid_bytes[i]));
  }
  return s;
}