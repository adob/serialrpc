#pragma once

import <initializer_list>;

import lib.str;
import lib.types;

namespace serialrpc {
    using namespace lib;

    enum class TypeKind : uint8 {
        Unknown = 0,
        Double = 1,
        Float = 2,
        Int64 = 3,
        Uint64 = 4,
        Int32 = 5,
        Fixed64 = 6,
        Fixed32 = 7,
        Bool = 8,
        String = 9,
        Message = 11,
        Bytes = 12,
        Uint32 = 13,
        Enum = 14,
        Sfixed32 = 15,
        Sfixed64 = 16,
        Sint32 = 17,
        Sint64 = 18,
    };

    struct FieldInfo;

    struct EnumValueInfo {
        str name;
        int32 number;
    };

    struct TypeInfo {
        TypeKind kind;
        str name;
        std::initializer_list<FieldInfo> fields;
        std::initializer_list<EnumValueInfo> enumValues;
    };

    struct FieldInfo {
        str name;
        uint32 number;
        TypeInfo const* type;
        bool repeated;
    };

    struct MethodInfo {
        str name;
        uint32 id;
        TypeInfo const* requestType;
        TypeInfo const* responseType;
        bool clientStreaming;
        bool serverStreaming;
    };

    inline constexpr TypeInfo TypeDouble{TypeKind::Double};
    inline constexpr TypeInfo TypeFloat{TypeKind::Float};
    inline constexpr TypeInfo TypeInt64{TypeKind::Int64};
    inline constexpr TypeInfo TypeUint64{TypeKind::Uint64};
    inline constexpr TypeInfo TypeInt32{TypeKind::Int32};
    inline constexpr TypeInfo TypeFixed64{TypeKind::Fixed64};
    inline constexpr TypeInfo TypeFixed32{TypeKind::Fixed32};
    inline constexpr TypeInfo TypeBool{TypeKind::Bool};
    inline constexpr TypeInfo TypeString{TypeKind::String};
    inline constexpr TypeInfo TypeBytes{TypeKind::Bytes};
    inline constexpr TypeInfo TypeUint32{TypeKind::Uint32};
    inline constexpr TypeInfo TypeSfixed32{TypeKind::Sfixed32};
    inline constexpr TypeInfo TypeSfixed64{TypeKind::Sfixed64};
    inline constexpr TypeInfo TypeSint32{TypeKind::Sint32};
    inline constexpr TypeInfo TypeSint64{TypeKind::Sint64};
    inline constexpr TypeInfo TypeVoid{TypeKind::Message, "void"};
}
