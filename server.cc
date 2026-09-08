#include <sys/unistd.h>

#include <cstring>

#include "lib/error.h"
#include "lib/varint/varint.h"
#include "lib/serial/serial_listener.h"

#include "server.h"
#include "rpc.h"
#include "serialrpc/encoding.h"
#include "serialrpc/generated/serialrpc_protocol.pb_msg.h"

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

void DiscoveryServiceImpl::dispatch_ListServices(void *service, serial::Conn &conn, int /*rpc_id*/, error err) {
    (void) unmarshal<serialrpcpb::ListServicesRequest>(conn, err);
    if (err) {
        return;
    }

    struct ServiceInfoView {
        ServiceDescription const& service;

        // Marshal directly from static service metadata. Building the generated
        // ServiceInfo value here would allocate its repeated methods vector.
        static void marshal(ServiceInfoView const& value, io::Writer &out,
                            error err, int nesting, Stack &stack) {
            auto const& service = value.service.Info;
            constexpr size MaxFullNameSize = 128;
            char full_name[MaxFullNameSize];
            size full_name_size = service.Name.size();

            if (!service.Package.empty()) {
                full_name_size += service.Package.size() + 1;
            }
            if (full_name_size > MaxFullNameSize) {
                err("serialrpc: fully qualified service name is too long");
                return;
            }

            size offset = 0;
            if (!service.Package.empty()) {
                memcpy(full_name, service.Package.data(), service.Package.size());
                offset = service.Package.size();
                full_name[offset++] = '.';
            }
            memcpy(full_name + offset, service.Name.data(), service.Name.size());

            marshal_field(out, serialrpcpb::ServiceInfo::NameFieldNumber,
                          str(full_name, full_name_size), err, nesting - 1, stack);
            if (err) {
                return;
            }
            marshal_field(out, serialrpcpb::ServiceInfo::UuidFieldNumber,
                          str(service.UUID), err, nesting - 1, stack);
            if (err) {
                return;
            }
            marshal_field(out,
                          serialrpcpb::ServiceInfo::MajorVersionFieldNumber,
                          int32(service.MajorVersion), err, nesting - 1, stack);
            if (err) {
                return;
            }
            marshal_field(out,
                          serialrpcpb::ServiceInfo::MinorVersionFieldNumber,
                          int32(service.MinorVersion), err, nesting - 1, stack);
            if (err) {
                return;
            }

            for (MethodInfo const& method : value.service.Methods) {
                serialrpcpb::MethodInfo method_info;
                method_info.name = str::from_c_str(method.name);
                method_info.id = method.id;
                marshal_field(out, serialrpcpb::ServiceInfo::MethodsFieldNumber,
                              method_info, err, nesting - 1, stack);
                if (err) {
                    return;
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
                      ServiceInfoView{description}, err, MaxNesting, stack);
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

    fmt::fprintf(os::stderr, "RPC error: %v\n", rpc_error);

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
