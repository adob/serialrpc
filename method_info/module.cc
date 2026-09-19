module;
#include "../method_info.h"

export module serialrpc.method_info;
export import lib.str;
export import lib.types;

export namespace serialrpc {
    using ::serialrpc::TypeKind;
    using ::serialrpc::FieldInfo;
    using ::serialrpc::EnumValueInfo;
    using ::serialrpc::TypeInfo;
    using ::serialrpc::MethodInfo;
    using ::serialrpc::TypeDouble;
    using ::serialrpc::TypeFloat;
    using ::serialrpc::TypeInt64;
    using ::serialrpc::TypeUint64;
    using ::serialrpc::TypeInt32;
    using ::serialrpc::TypeFixed64;
    using ::serialrpc::TypeFixed32;
    using ::serialrpc::TypeBool;
    using ::serialrpc::TypeString;
    using ::serialrpc::TypeBytes;
    using ::serialrpc::TypeUint32;
    using ::serialrpc::TypeSfixed32;
    using ::serialrpc::TypeSfixed64;
    using ::serialrpc::TypeSint32;
    using ::serialrpc::TypeSint64;
    using ::serialrpc::TypeVoid;
}
