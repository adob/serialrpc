#include <sys/unistd.h>

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

void serialrpc::ServerBase::handle_goodbye(error err) {
    ServerBase &s = *this;

    {
        sync::Lock lock(s.conn->write_mtx);

        s.conn->write_byte(byte(ServerGoodbye), err);
        s.conn->flush(err);
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
    s.conn = &conn;
    
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

    s.send_services_descriptions(err);
    if (err) {
        return;
    }

    s.conn->write_byte(byte(Tag::End), err);
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
            s.handle_goodbye(err);
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

    sync::Lock lock(s.conn->write_mtx);
    s.conn = nil;
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
void serialrpc::ServerBase::send_goodbye(error err) {
    ServerBase &s = *this;
    sync::Lock lock(s.conn->write_mtx);
    if (s.conn == nil) {
        return;
    }
    s.conn->write_byte(byte(ServerGoodbye), err);
    if (err) {
        return;
    }
    s.conn->flush(err);
    if (err) {
        return;
    }
}