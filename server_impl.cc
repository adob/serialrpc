#include <sys/unistd.h>

#include <cstring>
#include "rpc.h"

import lib.array;
import lib.error;
import lib.fmt;
import lib.inline_string;
import lib.io;
import lib.panic;
import lib.types;
import lib.varint;
import lib.serial;
import lib.sync;
import serialrpc.encoding;
import serialrpc.generated.serialrpc_protocol.msg;
import serialrpc.method_info;
import serialrpc.server;
import serialrpc.service_info;

#include "lib/print.h"

using namespace serialrpc;

void serialrpc::finish_msg(serial::Conn &conn, error err) {
    conn.flush(err);
}

void serialrpc::send_code(serial::Conn &conn, ServerMessageType code, error err) {    
    sync::Lock lock(conn.write_mtx);
    conn.write_byte(byte(code), err);
    if (err) {
        return;
    }

    conn.flush(err);
}

namespace {
    uint32 type_id(ServiceDescription const& service, TypeInfo const* type) {
        for (size i = 0; i < len(service.types); ++i) {
            if (service.types[i] == type) {
                return uint32(i + 1);
            }
        }
        return 0;
    }

    struct DiscoveredMethod {
        ServiceDescription const& service;
        MethodInfo const& method;
        bool full;

        static void marshal(DiscoveredMethod const& value, io::Writer &out,
                            error err, int nesting, Stack &stack) {
            marshal_field(out, serialrpcpb::MethodInfo::NameFieldNumber,
                          value.method.name, err, nesting - 1, stack);
            marshal_field(out, serialrpcpb::MethodInfo::IdFieldNumber,
                          value.method.id, err, nesting - 1, stack);
            if (!value.full || err) {
                return;
            }
            marshal_field(out, serialrpcpb::MethodInfo::RequestTypeFieldNumber,
                          type_id(value.service, value.method.requestType),
                          err, nesting - 1, stack);
            marshal_field(out, serialrpcpb::MethodInfo::ResponseTypeFieldNumber,
                          type_id(value.service, value.method.responseType),
                          err, nesting - 1, stack);
            marshal_field(out,
                          serialrpcpb::MethodInfo::ClientStreamingFieldNumber,
                          value.method.clientStreaming, err, nesting - 1, stack);
            marshal_field(out,
                          serialrpcpb::MethodInfo::ServerStreamingFieldNumber,
                          value.method.serverStreaming, err, nesting - 1, stack);
        }
    };

    struct DiscoveredField {
        ServiceDescription const& service;
        FieldInfo const& field;

        static void marshal(DiscoveredField const& value, io::Writer &out,
                            error err, int nesting, Stack &stack) {
            marshal_field(out, serialrpcpb::FieldInfo::NameFieldNumber,
                          value.field.name, err, nesting - 1, stack);
            marshal_field(out, serialrpcpb::FieldInfo::NumberFieldNumber,
                          value.field.number, err, nesting - 1, stack);
            marshal_field(out, serialrpcpb::FieldInfo::TypeFieldNumber,
                          int32(value.field.type->kind), err, nesting - 1, stack);
            if (value.field.type->kind == TypeKind::Message
                || value.field.type->kind == TypeKind::Enum) {
                marshal_field(out, serialrpcpb::FieldInfo::TypeIdFieldNumber,
                              type_id(value.service, value.field.type), err,
                              nesting - 1, stack);
            }
            marshal_field(out, serialrpcpb::FieldInfo::RepeatedFieldNumber,
                          value.field.repeated, err, nesting - 1, stack);
        }
    };

    struct DiscoveredType {
        ServiceDescription const& service;
        TypeInfo const& type;
        uint32 id;

        static void marshal(DiscoveredType const& value, io::Writer &out,
                            error err, int nesting, Stack &stack) {
            marshal_field(out, serialrpcpb::TypeInfo::IdFieldNumber,
                          value.id, err, nesting - 1, stack);
            marshal_field(out, serialrpcpb::TypeInfo::TypeFieldNumber,
                          int32(value.type.kind), err, nesting - 1, stack);
            marshal_field(out, serialrpcpb::TypeInfo::NameFieldNumber,
                          value.type.name, err, nesting - 1, stack);
            for (FieldInfo const& field : value.type.fields) {
                marshal_field(out, serialrpcpb::TypeInfo::FieldsFieldNumber,
                              DiscoveredField{value.service, field}, err,
                              nesting - 1, stack);
                if (err) {
                    return;
                }
            }
            for (EnumValueInfo const& enumValue : value.type.enumValues) {
                serialrpcpb::EnumValueInfo discovered;
                discovered.name = enumValue.name;
                discovered.number = enumValue.number;
                marshal_field(out,
                              serialrpcpb::TypeInfo::EnumValuesFieldNumber,
                              discovered, err, nesting - 1, stack);
                if (err) {
                    return;
                }
            }
        }
    };
}

void DiscoveryServiceImpl::dispatch_list_services(void *service, serial::Conn &conn, int /*rpc_id*/, error err) {
    auto request = unmarshal<serialrpcpb::ListServicesRequest>(conn, err);
    if (err) {
        return;
    }

    struct ServiceInfoView {
        ServiceDescription const& service;
        bool full;

        // Marshal directly from static service metadata. Building the generated
        // ServiceInfo value here would allocate its repeated methods vector.
        static void marshal(ServiceInfoView const& value, io::Writer &out,
                            error err, int nesting, Stack &stack) {
            auto const& service = value.service.info;
            constexpr size MaxFullNameSize = 128;
            lib::InlineString<MaxFullNameSize> full_name;

            if (service.package) {
                full_name += service.package;
                full_name += ".";
            }
            full_name += service.name;

            marshal_field(out, serialrpcpb::ServiceInfo::NameFieldNumber,
                          full_name, err, nesting - 1, stack);
            if (err) {
                return;
            }
            marshal_field(out, serialrpcpb::ServiceInfo::UuidFieldNumber,
                          str(service.uuid), err, nesting - 1, stack);
            if (err) {
                return;
            }
            marshal_field(out,
                          serialrpcpb::ServiceInfo::MajorVersionFieldNumber,
                          service.major_version, err, nesting - 1, stack);
            if (err) {
                return;
            }
            marshal_field(out,
                          serialrpcpb::ServiceInfo::MinorVersionFieldNumber,
                          service.minor_version, err, nesting - 1, stack);
            if (err) {
                return;
            }

            for (MethodInfo const& method : value.service.methods) {
                marshal_field(out,
                              serialrpcpb::ServiceInfo::MethodsFieldNumber,
                              DiscoveredMethod{value.service, method, value.full},
                              err, nesting - 1, stack);
                if (err) {
                    return;
                }
            }

            if (value.full) {
                for (size i = 0; i < len(value.service.types); ++i) {
                    marshal_field(out,
                                  serialrpcpb::ServiceInfo::TypesFieldNumber,
                                  DiscoveredType{value.service,
                                      *value.service.types[i], uint32(i + 1)},
                                  err, nesting - 1, stack);
                    if (err) {
                        return;
                    }
                }
            }
        }
    };

    DiscoveryServiceImpl &discovery =
        *static_cast<DiscoveryServiceImpl*>(service);
    sync::Lock lock(conn.write_mtx);

    start_reply(conn, err);
    if (err) {
        return;
    }

    Stack stack;
    for (ServiceDescription const& description : discovery.services) {
        marshal_field(conn,
                      serialrpcpb::ListServicesResponse::ServicesFieldNumber,
                      ServiceInfoView{description, request.full}, err,
                      MaxNesting, stack);
        if (err) {
            return;
        }
    }

    conn.write_byte(Tag::End, err);
    if (err) {
        return;
    }
    conn.flush(err);
}

ServerErrorHandler::ServerErrorHandler(serial::Conn &conn, error err)
        : conn(conn), err(err) {}

void ServerErrorHandler::handle(Error &rpc_error) {
    ServerErrorHandler &s = *this;

    fmt::fprintf(io::err, "RPC error: %v\n", rpc_error);

    sync::Lock lock(s.conn.write_mtx);
    s.conn.write_byte(byte(ServerMessageType::ErrorReply), s.err);
    if (s.err) {
        return;
    }

    serialrpc::write_chunked(s.conn, fmt::sprint(rpc_error), s.err);
    if (s.err) {
        return;
    }

    s.conn.flush(s.err);
}

void serialrpc::ServerBase::handle_goodbye(serial::Conn &conn, error err) {
    ServerBase &s = *this;

    {
        sync::Lock lock(conn.write_mtx);

        conn.write_byte(byte(ServerGoodbye), err);
        conn.flush(err);
    }
    if (err) {
        return;
    }

    s.stop_accept();
}
void serialrpc::start_reply(serial::Conn &conn, error err) {
    conn.write_byte(byte(Reply), err);
}

void serialrpc::start_event(serial::Conn &conn, uint32 event_id, error err) {
    conn.write_byte(byte(Event), err);
    if (err) {
        return;
    }

    varint::write_uint32(conn, event_id+1, err);
}
void serialrpc::send_event(serial::Conn &conn, uint32 event_id) {
  sync::Lock lock(conn.write_mtx);

  ErrorFunc err = [](Error &) {};
  start_event(conn, event_id, err);
  if (err) {
    return;
  }

  finish_msg(conn, err);
  if (err) {
    return;
  }
}

void ServerBase::server_hello(serial::Conn &conn, error err) {
    ServerBase &s = *this;
    sync::Lock lock(conn.write_mtx);
    
    conn.write_byte(byte(ServerHello), err);
    if (err) {
        return;
    }

    write_tag(conn, serialrpcpb::ServerHello::ProtocolVersionFieldNumber, Tag::VarInt, err);
    if (err) {
        return;
    }

    varint::write_uint32(conn, ProtocolVersion, err);
    if (err) {
        return;
    }

    s.send_services_descriptions(conn, err);
    if (err) {
        return;
    }

    conn.write_byte(byte(Tag::End), err);
    if (err) {
        return;
    }

    conn.flush(err);
}

void ServerBase::accept(serial::Conn &conn, error err) {
    ServerBase &s = *this;
    byte b = conn.read_byte(err);
    if (err) {
        return;
    }

    if (b != ClientMessageType::ClientHello) {
        sync::Lock lock(conn.write_mtx);
        conn.write("serialrpc: ignoring invalid input\n", err);
        conn.flush(err);
        return;
    }

    s.server_hello(conn, err);
    if (err) {
        return;
    }

    for (;;) {
        uint32 rpc_id = varint::read_uint32(conn, err);
        if (err) {
            s.stop_accept();
            return;
        }

        if (rpc_id == 0) {
            s.handle_goodbye(conn, err);
            if (err) {
                return;
            }
            return;
        }

        s.handle_request(rpc_id - 1, conn, err);
        if (err) {
            s.stop_accept();
            return;
        }
    }
}

void ServerBase::serve(serial::Listener &listener, error err) {
    ServerBase &s = *this;
    int cnt = 1;
    for (;;) {
        fmt::printf("Waiting for connection %d...", cnt++);

        serial::Conn &conn = listener.accept(err);
        if (err) {
            return;
        }

        fmt::printf(" connected\n");
        
        s.accept(conn, [](Error &e){
            eprint "serialrpc RPC error: %v" % e;
        });
    }
}

void ServerBase::stop_accept() {
    ServerBase &s = *this;
    
    s.unsubscribe_all();
}

void serialrpc::send_reply_void(serial::Conn &conn, error err) {
    sync::Lock lock(conn.write_mtx);

    start_reply(conn, err);
    if (err) {
        return;
    }

    finish_msg(conn, err);
    if (err) {
        return;
    }
}
void serialrpc::ServerBase::send_goodbye(serial::Conn &conn, error err) {
    ServerBase &s = *this;

    s.unsubscribe_all();

    sync::Lock lock(conn.write_mtx);
    conn.write_byte(byte(ServerGoodbye), err);
    if (err) {
        return;
    }
    conn.flush(err);
    if (err) {
        return;
    }
}
