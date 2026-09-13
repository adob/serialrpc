import lib.error;
import lib.fmt;
import lib.os.stdio;
#include "serialrpc/client.h"
#include "serialrpc/generated/serialrpc_protocol.pb_client.h"

#include "call.h"

#include <vector>

using namespace lib;

namespace {
    serialrpcpb::TypeInfo const *find_type(
        serialrpcpb::ServiceInfo const &service, uint32 id) {
        for (auto const &type : service.types) {
            if (type.id == id) {
                return &type;
            }
        }
        return nil;
    }

    str type_name(serialrpcpb::ServiceInfo const &service, uint32 id) {
        auto type = find_type(service, id);
        if (type != nil) {
            return type->name;
        }
        return "<unknown>";
    }

    str scalar_type_name(serialrpcpb::FieldType type) {
        using serialrpcpb::FieldType;

        switch (type) {
        case FieldType::FieldTypeDouble:
            return "double";
        case FieldType::FieldTypeFloat:
            return "float";
        case FieldType::FieldTypeInt64:
            return "int64";
        case FieldType::FieldTypeUint64:
            return "uint64";
        case FieldType::FieldTypeInt32:
            return "int32";
        case FieldType::FieldTypeFixed64:
            return "fixed64";
        case FieldType::FieldTypeFixed32:
            return "fixed32";
        case FieldType::FieldTypeBool:
            return "bool";
        case FieldType::FieldTypeString:
            return "string";
        case FieldType::FieldTypeBytes:
            return "bytes";
        case FieldType::FieldTypeUint32:
            return "uint32";
        case FieldType::FieldTypeSfixed32:
            return "sfixed32";
        case FieldType::FieldTypeSfixed64:
            return "sfixed64";
        case FieldType::FieldTypeSint32:
            return "sint32";
        case FieldType::FieldTypeSint64:
            return "sint64";
        default:
            return "<unknown>";
        }
    }

    str field_type_name(serialrpcpb::ServiceInfo const &service,
                        serialrpcpb::FieldInfo const &field) {
        using serialrpcpb::FieldType;

        if (field.type == FieldType::FieldTypeMessage
            || field.type == FieldType::FieldTypeEnum) {
            return type_name(service, field.type_id);
        }
        return scalar_type_name(field.type);
    }

    void print_type(serialrpcpb::ServiceInfo const &service,
                    serialrpcpb::TypeInfo const &type) {
        using serialrpcpb::FieldType;

        if (type.type == FieldType::FieldTypeMessage) {
            // void is serialrpc's built-in empty message, not a user-defined
            // request or response whose fields need documenting.
            if (str(type.name) == "void") {
                return;
            }

            fmt::printf("\nmessage %s {\n", str(type.name));
            for (auto const &field : type.fields) {
                str repeated = field.repeated ? str("repeated ") : str();
                fmt::printf("  %s%s %s = %v;\n", repeated,
                            field_type_name(service, field), str(field.name),
                            field.number);
            }
            fmt::printf("}\n");
        } else if (type.type == FieldType::FieldTypeEnum) {
            fmt::printf("\nenum %s {\n", str(type.name));
            for (auto const &value : type.enum_values) {
                fmt::printf("  %s = %v;\n", str(value.name), value.number);
            }
            fmt::printf("}\n");
        }
    }

    bool contains(std::vector<str> const &names, str name) {
        for (str existing : names) {
            if (existing == name) {
                return true;
            }
        }
        return false;
    }

    void usage(io::Writer &out) {
        fmt::fprintf(out,
            "Usage: serialrpc <command> <endpoint>\n"
            "       serialrpc call <endpoint> <service>.<method> [<request>]\n"
            "\n"
            "Commands:\n"
            "  list, ls    List services exposed by the endpoint\n"
            "  call        Call a unary RPC using server discovery\n");
    }

    void list_services(str endpoint, error err) {
        serialrpcpb::DiscoveryServiceStub discovery;
        auto client = serialrpc::connect(endpoint, {&discovery}, err);
        if (err) {
            return;
        }

        auto response = discovery.list_services({.full = true}, err);
        if (err) {
            client->close(error::ignore);
            return;
        }

        fmt::printf("Services:\n\n");
        for (auto const &service : response.services) {
            fmt::printf("service %s {\n", str(service.name));
            for (auto const &method : service.methods) {
                str request_type = type_name(service, method.request_type);
                str response_type = type_name(service, method.response_type);
                str request_stream = method.client_streaming
                    ? str("stream ") : str();
                str response_stream = method.server_streaming
                    ? str("stream ") : str();
                fmt::printf("  rpc %s(%s%s) returns (%s%s) {}\n",
                            str(method.name), request_stream, request_type,
                            response_stream, response_type);
            }
            fmt::printf("}\n\n");
        }

        fmt::printf("Types:\n");
        std::vector<str> printed_types;
        for (auto const &service : response.services) {
            for (auto const &type : service.types) {
                if (contains(printed_types, type.name)) {
                    continue;
                }
                printed_types.push_back(type.name);
                print_type(service, type);
            }
        }
        fmt::printf("\n");

        client->close(err);
    }
}

int main(int argc, char *argv[]) {
    ErrorFunc err = [](Error const &e) {
        fmt::fprintf(os::stderr, "serialrpc: %v\n", e);
    };

    str command;
    if (argc >= 2) {
        command = str::from_c_str(argv[1]);
    }

    if (argc == 2 && (command == "-h" || command == "--help")) {
        usage(os::stdout);
        return 0;
    }

    if (command == "list" || command == "ls") {
        if (argc != 3) {
            usage(os::stderr);
            return 2;
        }
        list_services(str::from_c_str(argv[2]), err);
    } else if (command == "call") {
        if (argc != 4 && argc != 5) {
            usage(os::stderr);
            return 2;
        }
        str request;
        if (argc == 5) {
            request = str::from_c_str(argv[4]);
        }
        serialrpc::cli::call(
            str::from_c_str(argv[2]), str::from_c_str(argv[3]), request, err);
    } else {
        fmt::fprintf(os::stderr, "serialrpc: unknown command %q\n", argv[1]);
        usage(os::stderr);
        return 2;
    }
    if (err) {
        return 1;
    }

    return 0;
}
