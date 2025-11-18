#include "server.h"
#include "lib/error.h"
#include "lib/io/io.h"
#include "lib/varint/varint.h"
#include "rpc.h"
#include "internal.h"
#include "lib/print.h"
#include "serial/serial_listener.h"
#include <sys/unistd.h>
// #include "zephyr/kernel.h"

#ifdef ESP_PLATFORM
    #include "../../m5stack/src/display.h"
    #define DISPLAY(...) display::println(__VA_ARGS__)
#else
    #define DISPLAY(...)
#endif

using namespace serialrpc;

static void discard_line(io::ReaderWriter &conn, error err) {
    for (;;) {
        byte b = conn.read_byte(err);
        if (err || b == '\n') {
            return;
        }
    }
}

void ServerBase::finish_msg(io::ReaderWriter &conn, error err) {
    conn.flush(err);
}

void ServerBase::send_code(io::ReaderWriter &conn, ServerMessageType code, error err) {
    conn.write_byte(byte(code), err);
    if (err) {
        return;
    }

    conn.flush(err);
}

ServerBase::ServerErrorHandler::ServerErrorHandler(ServerBase &base, error err)
        : base(base), err(err) {}

void ServerBase::ServerErrorHandler::handle(Error &rpc_error) {
    ServerErrorHandler &s = *this;

    fmt::fprintf(os::stderr, "RPC error: %v\n", rpc_error);

    sync::Lock lock(base.conn->write_mtx);
    base.conn->write_byte(byte(ServerMessageType::ErrorReply), s.err);
    if (s.err) {
        return;
    }

    serialrpc::write_chunked(*base.conn, fmt::sprint(rpc_error), s.err);
    if (s.err) {
        return;
    }

    base.conn->flush(s.err);
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

void ServerBase::accept(serial::Conn &conn, error err) {
    ServerBase &s = *this;
    byte b = conn.read_byte(err);
    if (err) {
        return;
    }

    if (b != ClientMessageType::ClientHello) {
        sync::Lock lock(conn.write_mtx);
        conn.write("invalid input; ignoring\n", err);
        conn.flush(err);
        print "invalid hello";
        return;
    }

    {
        sync::Lock lock(conn.write_mtx);
        s.conn = &conn;
        conn.write_byte(byte(ServerHello), err);
        conn.flush(err);
    }

    for (;;) {
        // uint64_t start = k_cycle_get_64();
        uint32 rpc_id = varint::read_uint32(conn, err);
        if (err) {
            s.stop_accept();
            return;
        }
        // print "seriarpc::accept pc id %d" % rpc_id;


        if (rpc_id == 0) {
            s.handle_goodbye(err);
            if (err) {
                return;
            }
            print "goodbye";
            return;
        }

        s.handle_request(rpc_id, conn, err);
        if (err) {
            s.stop_accept();
            return;
        }

        // uint64_t end = k_cycle_get_64();
        // uint64_t cycles = end - start;
        // double ms = cycles / 64'000'000.0 * 1'000.0;
        
        // printk("v2 handling took %f ms\n", ms);
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
            #ifdef ESP_PLATFORM
            fmt::fprintf(display::writer, "serialrpc RPC error: <%v>\n", e);
            #else
            print "serialrpc RPC error: %v" % e;
            #endif
        });
        // printk("accept finished\n");
    }
}

void ServerBase::stop_accept() {
    ServerBase &s = *this;
    
    s.unsubscribe_all();

    sync::Lock lock(s.conn->write_mtx);
    s.conn = nil;
}

// void ServerBase::fail() {
//     ServerBase &s = *this;
    
//     s.unsubscribe_all();
//     s.conn = nil;
// }

void serialrpc::ServerBase::send_reply_void(io::ReaderWriter &conn, error err) {
    ServerBase &s = *this;
    sync::Lock lock(s.conn->write_mtx);

    start_reply(conn, err);
    if (err) {
        // s.fail();
        return;
    }

    finish_msg(*s.conn, err);
    if (err) {
        // s.fail();
        return;
    }
}
