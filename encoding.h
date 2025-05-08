#pragma once

#include "lib/base.h"
#include "lib/io.h"
#include "lib/io/io.h"
#include "lib/varint.h"
#include "lib/varint/varint.h"

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

    Tag read_tag(str *data, error err);
    Tag read_tag(io::Reader &in, error err);

    template <typename T>
    void marshal_fields(io::Writer &out, T const& t, error err, int nesting = 128) {
        T::marshal(t, out, err, nesting);
    }

    template <typename T>
    void marshal(io::Writer &out, T const& t, error err, int nesting = 128) {
        T::marshal(t, out, err, nesting);
        if (err) {
            return;
        }
        out.write_byte(Tag::End, err);
    }
    template <>
    void marshal<int32>(io::Writer &out, int32 const& t, error err, int nesting);
    
    template <>
    void marshal<uint32>(io::Writer &out, uint32 const& t, error err, int nesting);

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

    template <typename T>
    T unmarshal(io::Reader &in, error err, int nesting = 128) {
        if (nesting < 0) {
            err("excessive nesting");
            return {};
        }
        return T::unmarshal(in, err, nesting);
    }

    template <>
    int32 unmarshal<int32>(io::Reader &in, error err, int /*nesting*/);

    template <>
    uint32 unmarshal<uint32>(io::Reader &in, error err, int /*nesting*/);

    void write_tag(io::Writer &out, int32 field_number, Tag::Type type, error err);

    template <typename T>
    void marshal_field(io::Writer &out, int32 field_number, T const &t, error err, int nesting = 128) {
        // if (t == T{}) {
        //     return;
        // }
        write_tag(out, field_number, Tag::Start, err);
        if (err) {
            return;
        }
        
        marshal(out, t, err, nesting);
        if (err) {
            return;
        }
    }

    template <>
    void marshal_field<int32>(io::Writer &out, int32 field_number, int32 const &val, error err, int nesting);

    void skip(str *data, Tag::Type type, error err, int nesting = 128);
    void skip(io::Reader &in, Tag::Type type, error err, int nesting = 128);

    void write_chunked(io::Writer &out, io::WriterTo const &msg, error err);
    String read_chunked(io::Reader &in, error err);
}
