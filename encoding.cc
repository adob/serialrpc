#include <algorithm>
#include <bit>

#include "lib/base.h"
#include "lib/io/io.h"
#include "lib/io/util.h"
#include "lib/varint/varint.h"

#include "encoding.h"

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
        if (nesting < 0) {
            err("excessive nesting");
            return;
        }
        for (;;) {
            Tag tag = serialrpc::read_tag(in, err);
            if (err) {
                return;
            }

            if (tag.type == Tag::End) {
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

void serialrpc::skip(io::Reader &in, error err) {
    Tag::Type tag = (Tag::Type) in.read_byte(err);
    if (err) {
        return;
    }
    skip(in, tag, err, 128);
}

template <>
int32 serialrpc::unmarshal<int32>(io::Reader &in, error err, int /*nesting*/) {
    return varint::read_sint32(in, err);
}

template <>
uint32 serialrpc::unmarshal<uint32>(io::Reader &in, error err, int /*nesting*/) {
    return varint::read_uint32(in, err);
}

template <>
int64 serialrpc::unmarshal<int64>(io::Reader &in, error err, int /*nesting*/) {
    return varint::read_sint64(in, err);
}

template <>
uint64 serialrpc::unmarshal<uint64>(io::Reader &in, error err, int /*nesting*/) {
    return varint::read_uint64(in, err);
}

template <>
float32 serialrpc::unmarshal<float32>(io::Reader &in, error err, int /*nesting*/) {
    static_assert(sizeof(float32) == 4, "float must be 32-bit IEEE-754");
    byte bytes[4];
    io::read_full(in, bytes, err);
    if (err) {
        return 0;
    }

    uint32_t bits =
        (uint32_t(bytes[0]) << 0)  |
        (uint32_t(bytes[1]) << 8)  |
        (uint32_t(bytes[2]) << 16) |
        (uint32_t(bytes[3]) << 24);

    return std::bit_cast<float32>(bits);
}

template <>
float64 serialrpc::unmarshal<float64>(io::Reader &in, error err, int /*nesting*/) {
    static_assert(sizeof(float64) == 8, "float must be 64-bit IEEE-754");
    byte bytes[8];
    io::read_full(in, bytes, err);
    if (err) {
        return 0;
    }

    uint64_t bits =
        (uint64_t(bytes[0]) << 0)  |
        (uint64_t(bytes[1]) << 8)  |
        (uint64_t(bytes[2]) << 16) |
        (uint64_t(bytes[3]) << 24) |
        (uint64_t(bytes[4]) << 32) |
        (uint64_t(bytes[5]) << 40) |
        (uint64_t(bytes[6]) << 48) |
        (uint64_t(bytes[7]) << 56);

    return std::bit_cast<float64>(bits);
}


template <>
bool serialrpc::unmarshal<bool>(io::Reader &, error, int /*nesting*/) {
    return true;
}

template <>
str serialrpc::unmarshal<str>(io::Reader &in, error err, int /*nesting*/) {
    size n = varint::read_unsigned<size>(in, err);
    if (err) {
        return {};
    }

    str s = in.skip(n, err);
    if (err) {
        return {};
    }
    if (len(s) != n) {
        err("str unmarhsal short read");
    }
    return s;
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

// int32
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

// int64
void serialrpc::marshal_field(io::Writer &out, int32 field_number, int64 val, error err, int /*nesting*/, Stack &stack) {
     if (val == 0) {
        return;
    }

    write_tags(out, field_number, Tag::VarInt, stack, err);
    if (err) {
        return;
    }

    varint::write_sint64(out, val, err);
}

// uint32
void serialrpc::marshal_field(io::Writer &out, int32 field_number, uint32 val, error err, int /*nesting*/, Stack &stack) {
    if (val == 0) {
        return;
    }
    
    write_tags(out, field_number, Tag::VarInt, stack, err);
    if (err) {
        return;
    }
    
    varint::write_uint32(out, val, err);
}


// uint64
void serialrpc::marshal_field(io::Writer &out, int32 field_number, uint64 val, error err, int /*nesting*/, Stack &stack) {
    if (val == 0) {
        return;
    }
    
    write_tags(out, field_number, Tag::VarInt, stack, err);
    if (err) {
        return;
    }
    
    varint::write_uint64(out, val, err);
}


void serialrpc::marshal_field(io::Writer &out, int32 field_number, str s, error err, int /*nesting*/, Stack &stack) {
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

    write_tag(out, field_number, Tag::Len, err);
    if (err) {
        return;
    }

    varint::write_unsigned(out, len(s),err);
    if (err) {
        return;
    }

    out.write(s, err);
}

void serialrpc::marshal_field(io::Writer &out, int32 field_number, bool val, error err, int /*nesting*/, Stack &stack) {
    if (!val) {
        return;
    }

    for (uint32 tag : stack) {
        write_tag(out, tag, Tag::Start, err);
        if (err) {
            return;
        }
    }
    stack.clear();

    write_tag(out, field_number, Tag::Len, err);
    if (err) {
        return;
    }
}

// void marshal_field(io::Writer &out, int32 field_number, uint64 val, error err, int nesting, Stack &stack);

void serialrpc::marshal_field(io::Writer &out, int32 field_number, float32 val, error err, int nesting, Stack &stack) {
    if (val == 0) {
        return;
    }
    
    write_tags(out, field_number, Tag::I32, stack, err);
    if (err) {
        return;
    }

    static_assert(sizeof(float32) == 4, "float must be 32-bit IEEE-754");


    auto b = std::bit_cast<std::array<byte, 4>>(val);
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(b.begin(), b.end());
    }

    out.write(str(b.data(), b.size()), err);
}

void serialrpc::marshal_field(io::Writer &out, int32 field_number, double val, error err, int nesting, Stack &stack) {
    if (val == 0) {
        return;
    }
    
    write_tags(out, field_number, Tag::I64, stack, err);
    if (err) {
        return;
    }

    static_assert(sizeof(float32) == 4, "float must be 64-bit IEEE-754");

    auto b = std::bit_cast<std::array<byte, 8>>(val);
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(b.begin(), b.end());
    }

    out.write(str(b.data(), b.size()), err);
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

        if (len(s) + n > MaxStringSize) {
            err("chunked message too big");
            return s;
        }

        buf b = s.expand(n);
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
        return 0;
    }

    io::read_full(in, bytes[0, n], err);
    return n;
}
