#include "server.h"
#include "lib/error.h"
#include "lib/io/io.h"
#include "lib/varint/varint.h"
#include "rpc.h"
#include "internal.h"
#include "lib/print.h"

using namespace serialrpc;

static void discard_line(io::ReaderWriter &conn, error err) {
    for (;;) {
        byte b = conn.read_byte(err);
        if (err || b == '\n') {
            return;
        }
    }
}

Server::Server(Service &service) : service(service) {}


void Server::serve(io::ReaderWriter &conn, error err) {
    for (;;) {
        bool b = handle_rpc(conn, err);
        if (!b || err) {
            return;
        }
    }
}

bool Server::handle_rpc(io::ReaderWriter &conn, error err) {
    uint8 n = conn.read_byte(err);
    if (err) {
        return false;
    }

     if (is_printable(n)) {
        conn.write("invalid text input; ignoring until end of line\n", err);
        if (err) {
            return false;
        }

        discard_line(conn, err);
        return true;
    }

    if ((n & 0xF0) != 0xF0) {
        conn.write_byte((byte) FatalError, error::ignore);
        print "serialrpc server: received invalid start byte %#X" % n;
        err(ErrCorruption());
        return false;
    }

    ClientMessageType type = ClientMessageType(n);
    switch (type) {
    case Request:
        print "got requet";
        handle_request(conn, err);
        return true;

    case ClientHello:
        print "got client hello";
        handle_hello(conn, err);
        return true;

    case ClientGoodbye:
        print "got client goodbye";
        handle_goodbye(conn, err);
        return false;

    default:
        print "serialrpc server: unrecognized message type: %#X" % (int) type;
        conn.write_byte((byte) BadMessage, err);
    }
    return true;
}

void Server::handle_request(io::ReaderWriter &conn, error err) {
    uint32 method_id = varint::read_uint32(conn, err);
    if (err) {
        return;
    }

    size body_size = varint::read_uint32(conn, err);
    if (err) {
        return;
    }

    // handle too big

    buf body_data = readbuf[0, body_size];
    io::read_full(conn, body_data, err);
    if (err) {
        return;
    }


    CallCtx ctx {
        .server = *this,
        .conn   = conn,
        .err    = err,
    };

    bool error_occured = false;
    service.handle(ctx, method_id, body_data, [&](Error &e) {
        if (error_occured) {
            return;
        }

        io::Buf b(this->writebuf);
        e.fmt(b, error::ignore);
        ctx.send_error(b.bytes());
        error_occured = true;
    });

    if (error_occured) {
        return;
    }

    if (!ctx.handled) {
        conn.write_byte((byte) Unknown, err);
        if (err) {
            return;
        }
    }
}

void Server::handle_goodbye(io::ReaderWriter &conn, error err) {
    conn.write_byte((byte) ServerGoodbye, err);
    conn.flush(err);
}

void Server::handle_hello(io::ReaderWriter &conn, error err) {
    conn.write_byte((byte) ServerHello, err);
    conn.flush(err);
}

void CallCtx::reply(str data) {
    if (handled) {
        panic("already handled");
    }
    handled = true;
    conn.write_byte((byte) ServerMessageType::Reply, err);

    varint::write_uint32(conn, len(data), err);
    conn.write(data, err);
    conn.flush(err);
}


void CallCtx::send_code(ServerMessageType code) {
    if (handled) {
        panic("already handled");
    }
    handled = true;
    conn.write_byte((byte) code, err);
    conn.flush(err);
}


void CallCtx::send_error(str data) {
    if (handled) {
        panic("already handled");
    }
    handled = true;
    conn.write_byte((byte) ServerMessageType::ErrorReply, err);

    varint::write_uint32(conn, len(data), err);
    conn.write(data, err);
    conn.flush(err);
}

void ServerBase::finish_msg(io::ReaderWriter &conn, error err) {
    conn.write_byte(byte(Tag::End), err);
    if (err) {
        return;
    }
    conn.flush(err);
}

void ServerBase::send_code(io::ReaderWriter &conn,
                                      ServerMessageType code, error err) {
    conn.write_byte(byte(code), err);
    if (err) {
        return;
    }

    conn.flush(err);
}

void serialrpc::ServerBase::send_error(io::ReaderWriter &conn, error const &rpc_error, error err) {
    conn.write_byte(byte(ServerMessageType::ErrorReply), err);
    if (err) {
        return;
    }

    serialrpc::write_chunked(conn, fmt::sprint(rpc_error), err);
    if (err) {
        return;
    }

    conn.flush(err);
}

ServerBase::ServerErrorHandler::ServerErrorHandler(io::ReaderWriter &conn, error err)
        : conn(conn), err(err) {}

void ServerBase::ServerErrorHandler::report(Error &rpc_error) {
    conn.write_byte(byte(ServerMessageType::ErrorReply), err);
    if (err) {
        return;
    }

    serialrpc::write_chunked(conn, fmt::sprint(rpc_error), err);
    if (err) {
        return;
    }

    conn.flush(err);
}
void ServerBase::serve_request(io::ReaderWriter &conn, error err) {
    ServerBase &s = *this;
    uint32 rpc_id = varint::read_uint32(conn, err);
    if (err) {
        return;
    }

    if (rpc_id == 0) {
        s.handle_goodbye(err);
        return;
    }

    print "server got rpc_id", rpc_id;

    s.handle_request(rpc_id, conn, err);
    if (err) {
        return;
    }
}

void serialrpc::ServerBase::handle_goodbye(error err) {
    ServerBase &s = *this;

    s.conn->write_byte(byte(ServerGoodbye), err);
    s.conn->flush(err);
    s.stop_accept();
}
void serialrpc::ServerBase::start_reply(io::ReaderWriter &conn, error err) {
    conn.write_byte(byte(Reply), err);
}

void ServerBase::start_event(uint32 event_id, error err) {
    ServerBase &s = *this;
    s.conn->write_byte(byte(Event), err);
    if (err) {
        return;
    }

    varint::write_uint32(*s.conn, event_id, err);
}

void ServerBase::accept(io::ReaderWriter &conn, error err) {
    ServerBase &s = *this;

    byte b = conn.read_byte(err);
    if (err) {
        return;
    }

    if (b != ClientMessageType::ClientHello) {
        conn.write("invalid input; ignoring\n", err);
        conn.flush(err);
        return;
    }

    {
        sync::Lock lock(s.send_mtx);
        s.conn = &conn;
        conn.write_byte(byte(ServerHello), err);
        conn.flush(err);
    }

    for (;;) {
        uint32 rpc_id = varint::read_uint32(conn, err);
        if (err) {
            stop_accept();
            return;
        }

        print "server got rpc_id", rpc_id;

        if (rpc_id == 0) {
            print "server: sending goodbye...";
            s.handle_goodbye(err);
            print "server: sent goodbye, returning";
            return;
        }

        s.handle_request(rpc_id, conn, err);
        if (err) {
            s.stop_accept();
            return;
        }
    }
}

void ServerBase::stop_accept() {
    ServerBase &s = *this;
    
    s.unsubscribe_all();

    sync::Lock lock(s.send_mtx);
    s.conn = nil;
}

void ServerBase::fail() {
    ServerBase &s = *this;
    
    s.unsubscribe_all();
    s.conn = nil;
}