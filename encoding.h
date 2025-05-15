#pragma once

#include "lib/base.h"
#include "lib/inline_string.h"
#include "lib/io.h"
#include "lib/io/io.h"
#include "lib/varint.h"
#include "lib/varint/varint.h"
#include <concepts>

namespace serialrpc {
    using namespace lib;

    struct Archiver {
        io::Reader &out;
        error &err;

        template <typename ...Args>
        void operator () (Args &&...args) {
            // https://www.foonathan.net/2020/05/fold-tricks/
            (void) ( (write(args), bool(err)) || ... );

            //(*this << ... << args);
        }

        void write(int32);
        void write(float64 n);
        void write(bool b);

        template <typename T>
        void write(T &t) {
            t.serialize(*this);
        }

    private:
        void write_fixed(str);
    };

    struct UnArchiver {
        io::Writer &in;
        error err;

        template <typename ...Args>
        void operator () (Args &&...args) {
            (void) ( (read(args), bool(err)) || ... );
        }

        void read(int32 &n);
        void read(float64 &n);
        void read(bool &b);

        template <typename T>
        void read(T &t) {
            t.serialize(*this);
        }
    };

    template <typename T>
    str serialize(buf buffer, T const& t, error err) {
        io::Buf out(buffer);

        Archiver ar { out, err };
        const_cast<T&>(t).serialize(ar);

        return out.bytes();

    }

    template <typename T>
    void deserialize(str buffer, T &t, error err) {
        io::Str in(buffer);

        UnArchiver ar { in, err };
        t.serialize(ar);
    }

    struct Tag {
        enum Type : byte {
            VarInt = 0,
            I64 = 1,
            Len = 2,
            Start = 3,
            End = 4,
            I32 = 5,
        } ;

        Type type = {};
        int32 field_num = 0;
    } ;

    // Tag read_tag(str *data, error err);
    Tag read_tag(io::Reader &in, error err);

    const int MaxNesting = 128;

    struct Stack {
        int size = 0;
        uint32 elems[MaxNesting];

        void push(uint32 e) {
            if (size == MaxNesting) {
                panic("out of space");
            }
            elems[size++] = e;
        }

        void pop() {
            if (size == 0) {
                panic("empty stack");
            }
            size--;
        }

        void clear() {
            size = 0;
        }

        uint32 *begin() { return elems; }
        uint32 *end() { return elems+size; }
    } ;

    // template <typename T>
    // void marshal_fields(io::Writer &out, T const& t, error err, int nesting, Stack &stack) {
    //     T::marshal(t, out, err, nesting, stack);
    // }

    void write_tag(io::Writer &out, int32 field_number, Tag::Type type, error err);

    template <typename T>
    concept Marshallable = requires(T const& t, io::Writer &out, io::Reader &in, error err, int nesting, Stack &stack) {
        { T::marshal(t, out, err, nesting, stack) };
        { T::unmarshal(in, err, nesting) };
    };

    template <Marshallable T>
    void marshal_field(io::Writer &out, int32 field_number, T const &t, error err, int nesting, Stack &stack) {
        stack.push(field_number);
        
        T::marshal(t, out, err, nesting, stack);
        if (err) {
            return;
        }

        if (stack.size == 0) {
            out.write_byte(Tag::End, err);
        } else {
            stack.pop();
        }
    }
    
    void marshal_field(io::Writer &out, int32 field_number, int32 val, error err, int nesting, Stack &stack);
    void marshal_field(io::Writer &out, int32 field_number, uint32 val, error err, int nesting, Stack &stack);

    void marshal_field(io::Writer &out, int32 field_numer, str s, error err, int nesting, Stack &stack);

    template <typename T>
    void marshal(io::Writer &out, T const& t, error err) {
        Stack stack;
        T::marshal(t, out, err, MaxNesting, stack);
        if (err) {
            return;
        }
        out.write_byte(Tag::End, err);
    }

    // template <>
    // void marshal<int32>(io::Writer &out, int32 const& t, error err, int nesting, Stack &stack);
    
    // template <>
    // void marshal<uint32>(io::Writer &out, uint32 const& t, error err, int nesting, Stack &stack);

    // template <typename T>
    // T unmarshal(str *data, error err, int nesting = 128) {
    //     if (nesting < 0) {
    //         err("excessive nesting");
    //         return {};
    //     }
    //     return T::unmarshal(data, err, nesting);
    // }

    // template <>
    // int32 unmarshal<int32>(str *data, error err, int /*nesting*/);

    // template <>
    // uint32 unmarshal<uint32>(str *data, error err, int /*nesting*/);

    template <Marshallable T>
    T unmarshal(io::Reader &in, error err, int nesting = 128) {
        if (nesting < 0) {
            err("excessive nesting");
            return {};
        }
        return T::unmarshal(in, err, nesting);
    }

    size unmarshal_bytes(io::Reader &in, buf bytes, error err);

    template<typename T>
    struct is_inline_string : std::false_type {};

    template<size N>
    struct is_inline_string<InlineString<N>> : std::true_type {};

    template<typename T>
    concept InlineString = is_inline_string<T>::value;

    template <InlineString T>
    T unmarshal(io::Reader &in, error err, int /*nesting*/ = 128) {
        T t;
        t.length = unmarshal_bytes(in, t.data(), err);
        return t;
    }

    template <typename T>
    T unmarshal(io::Reader &in, error err, int /*nesting*/ = 128) {
        static_assert(false);
    }

    template <>
    int32 unmarshal<int32>(io::Reader &in, error err, int /*nesting*/);

    template <>
    uint32 unmarshal<uint32>(io::Reader &in, error err, int /*nesting*/);

    // template <size N>
    // InlineString<N> unmarshal(io::Reader &in, error err, int /*nesting*/) {
    //     panic("!");
    //     return {};
    // }

    // void skip(str *data, Tag::Type type, error err, int nesting = 128);
    void skip(io::Reader &in, Tag::Type type, error err, int nesting = 128);

    void write_chunked(io::Writer &out, io::WriterTo const &msg, error err);
    String read_chunked(io::Reader &in, error err);
}
