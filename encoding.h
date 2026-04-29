#pragma once

#include <vector>

#include "lib/base.h"
#include "lib/inline_string.h"
#include "lib/io/io.h"

namespace serialrpc {
    using namespace lib;

    struct Archiver {
        io::Reader &out;
        error &err;

        template <typename ...Args>
        void operator () (Args &&...args) {
            // https://www.foonathan.net/2020/05/fold-tricks/
            (void) ( (write(args), bool(err)) || ... );
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
    void marshal_field(io::Writer &out, int32 field_number, int64 val, error err, int nesting, Stack &stack);
    void marshal_field(io::Writer &out, int32 field_number, uint64 val, error err, int nesting, Stack &stack);
    void marshal_field(io::Writer &out, int32 field_number, bool val, error err, int nesting, Stack &stack);
    void marshal_field(io::Writer &out, int32 field_number, float32 val, error err, int nesting, Stack &stack);
    void marshal_field(io::Writer &out, int32 field_number, float64 val, error err, int nesting, Stack &stack);

    void marshal_field(io::Writer &out, int32 field_numer, str s, error err, int nesting, Stack &stack);

    template <typename T>
    void marshal_field(io::Writer &out, int32 field_number, std::vector<T> const &vec, error err, int nesting, Stack &stack) {
        panic("not implemented");
    }

    template <typename T>
    void marshal(io::Writer &out, T const& t, error err) {
        Stack stack;
        T::marshal(t, out, err, MaxNesting, stack);
        if (err) {
            return;
        }
        out.write_byte(Tag::End, err);
    }

    template <Marshallable T>
    T unmarshal(io::Reader &in, error err, int nesting = 128) {
        if (nesting < 0) {
            err("excessive nesting");
            return {};
        }
        return T::unmarshal(in, err, nesting);
    }

    void skip(io::Reader &in, error err);

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

    template<class T>
    concept std_vector = requires { []<class X, class A>(std::vector<X, A> const&){}(std::declval<std::remove_cvref_t<T> const&>()); };

    template <typename T>
    T unmarshal(io::Reader &, error, int /*nesting*/ = 128) {
        if constexpr (std_vector<T>) {
            panic("unmarshal vector not implemented");
            return {};
        } else {
            static_assert(false);
        }
    }

    template <>
    int32 unmarshal<int32>(io::Reader &in, error err, int /*nesting*/);

    template <>
    uint32 unmarshal<uint32>(io::Reader &in, error err, int /*nesting*/);

    template <>
    int64 unmarshal<int64>(io::Reader &in, error err, int /*nesting*/);

    template <>
    uint64 unmarshal<uint64>(io::Reader &in, error err, int /*nesting*/);

    template <>
    float32 unmarshal<float32>(io::Reader &in, error err, int /*nesting*/);
    
    template <>
    float64 unmarshal<float64>(io::Reader &in, error err, int /*nesting*/);

    template <>
    bool unmarshal<bool>(io::Reader &in, error err, int /*nesting*/);

    template <>
    str unmarshal<str>(io::Reader &in, error err, int /*nesting*/);

    void skip(io::Reader &in, Tag::Type type, error err, int nesting = 128);

    void write_chunked(io::Writer &out, io::WriterTo const &msg, error err);
    String read_chunked(io::Reader &in, error err);
}
