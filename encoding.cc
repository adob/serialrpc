#include "encoding.h"
#include "lib/io.h"
#include "lib/io/io.h"
#include "lib/io/util.h"
#include "lib/print.h"
#include "lib/varint/varint.h"

using namespace lib;
using namespace serialrpc;

void Archiver::write(int32 n) {
    varint::write_sint32(out, n, err);
}

void UnArchiver::read(int32 &n) {
    n = varint::read_sint32(in, err);
}

void Archiver::write(float64 n) {
    str s((byte*) &n, sizeof(n));
    out.write(s, err);
}

void UnArchiver::read(float64 &n) {
    buf b((byte*) &n, sizeof(n));
    io::read_full(in, b, err);
}


void Archiver::write(bool b) {
    out.write_byte(b ? 1 : 0, err);
}

void UnArchiver::read(bool &b) {
    b = in.read_byte(err) != 0;
}

// Tag serialrpc::read_tag(str *data, error err) {
//     int32 n = varint::read_uint32(data, err);
//     if (err) {
//         return {};
//     }

//     Tag::Type type = Tag::Type(n & 7);
//     int32 field_num = n >> 3;
//     return {type, field_num};
// }

Tag serialrpc::read_tag(io::Reader &in, error err) {
    int32 n = varint::read_uint32(in, err);
    if (err) {
        return {};
    }

    Tag::Type type = Tag::Type(n & 7);
    int32 field_num = n >> 3;
    return {type, field_num};
}

void serialrpc::write_tag(io::Writer &out, int32 field_number, Tag::Type type, error err) {
    uint32 val = (field_number << 3) | uint8(type);
    varint::write_uint32(out, val, err);
}


// void serialrpc::skip(str *data, Tag::Type type, error err, int nesting) {
//     switch (type) {
//     case Tag::VarInt:
//         varint::skip(data, err);
//         break;

//     case Tag::I64:
//         if (len(*data) < 8) {
//             err(io::ErrUnexpectedEOF());
//         }
//         (*data) = *data + 8;
//         break;

//     case Tag::Len: {
//         size n = varint::read_uint32(data, err);
//         if (err) {
//             return;
//         }
//         if (len(*data) < n) {
//             err(io::ErrUnexpectedEOF());
//             return;
//         }
//         (*data) = *data + n;
//         break;
//     }

//     case Tag::Start:
//         for (;;) {
//             Tag tag = serialrpc::read_tag(data, err);
//             if (err) {
//                 return;
//             }
//             if (tag.type == Tag::End) {
//                 return;
//             }
//             skip(data, tag.type, err, nesting-1);
//             if (err) {
//                 return;
//             }
//         }
//         break;

    
//     case Tag::I32:
//         if (len(*data) < 4) {
//             err(io::ErrUnexpectedEOF());
//         }
//         (*data) = *data + 4;
//         break;
    
//     default:
//         err("serialrpc: invalid tag");
//         return;
//     }
// }

void serialrpc::skip(io::Reader &in, Tag::Type type, error err, int nesting) {
    switch (type) {
    case Tag::VarInt:
        varint::skip(in, err);
        return;

    case Tag::I64: {
        io::discard(in, 8, err);
        return;
    }

    case Tag::Len: {
        size n = varint::read_uint32(in, err);
        if (err) {
            return;
        }
        io::discard(in, n, err);
        return;
    }

    case Tag::Start:
        for (;;) {
            Tag tag = serialrpc::read_tag(in, err);
            if (err) {
                return;
            }
            skip(in, tag.type, err, nesting-1);
            if (err) {
                return;
            }
        }
        break;

    case Tag::End: {
        return;
    }
    
    case Tag::I32:
        io::discard(in, 4, err);
        return;
    
    default:
        err("serialrpc: invalid tag");
        return;
    }
}

// template <>
// void serialrpc::marshal<int32>(io::Writer &out, int32 const& t, error err, int /*nesting*/) {
//     varint::write_sint32(out, t, err);
// }

// template <>
// void serialrpc::marshal<uint32>(io::Writer &out, uint32 const& t, error err, int /*nesting*/) {
//     varint::write_uint32(out, t, err);
// }

template <>
int32 serialrpc::unmarshal<int32>(io::Reader &in, error err, int /*nesting*/) {
    return varint::read_sint32(in, err);
}

template <>
uint32 serialrpc::unmarshal<uint32>(io::Reader &in, error err, int /*nesting*/) {
    return varint::read_uint32(in, err);
}

static void write_tags(io::Writer &out, int32 field_number, Tag::Type tag, Stack &stack, error err) {
    for (uint32 elem : stack) {
        write_tag(out, elem, Tag::Start, err);
        if (err) {
            return;
        }
    }
    stack.clear();

    write_tag(out, field_number, tag, err);
}

void serialrpc::marshal_field(io::Writer &out, int32 field_number, int32 val, error err, int /*nesting*/, Stack &stack) {
    if (val == 0) {
        return;
    }

    write_tags(out, field_number, Tag::VarInt, stack, err);
    if (err) {
        return;
    }

    varint::write_sint32(out, val, err);
}


void serialrpc::marshal_field(io::Writer &out, int32 field_number, uint32 val, error err, int /*nesting*/, Stack &stack) {
    if (val == 0) {
        return;
    }
    
    write_tag(out, field_number, Tag::VarInt, err);
    if (err) {
        return;
    }
    
    varint::write_uint32(out, val, err);
}

void serialrpc::marshal_field(io::Writer &out, int32 field_numer, str s, error err, int nesting, Stack &stack) {
    if (len(s) == 0) {
        return;
    }

    for (uint32 tag : stack) {
        write_tag(out, tag, Tag::Start, err);
        if (err) {
            return;
        }
    }
    stack.clear();

    write_tag(out, field_numer, Tag::Len, err);
    if (err) {
        return;
    }

    varint::write_unsigned(out, len(s),err);
    if (err) {
        return;
    }

    out.write(s, err);
}


void serialrpc::write_chunked(io::Writer &out, io::WriterTo const &msg,
                              error err) {
  struct Writer : io::Writer {
    io::Writer &out;

    Writer(io::Writer &out) : out(out) {}

    size direct_write(str data, error err) override {
        varint::write_uint32(out, uint32(len(data)), err);
        if (err) {
            return 0;
        }

        return out.write(data, err);
    }
  } writer(out);

  msg.write_to(writer, err);
  if (err) {
    return;
  }
  out.write_byte(byte(0), err);
}

lib::String serialrpc::read_chunked(io::Reader &in, error err) {
    String s;

    for (;;) {
        uint32 n = varint::read_uint32(in, err);
        // print "read_chunked", n;
        if (err) {
            return s;
        }
        if (n == 0) {
            break;
        }

        buf b = s.expand(n);
        // print "buffer", len(b);
        io::read_full(in, b, err);
        if (err) {
            return s;
        }   
    }

    return s;
}

size serialrpc::unmarshal_bytes(io::Reader &in, buf bytes, error err) {
    size n = varint::read_unsigned<size>(in, err);
    if (err) {
        return n;
    }

    if (n > len(bytes)) {
        err("data too big");
    }

    io::read_full(in, bytes[0, n], err);
    return n;
}
