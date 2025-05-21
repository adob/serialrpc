#include "client.h"
#include "lib/io/util.h"
#include "lib/varint/varint.h"
#include "rpc.h"
#include "internal.h"
#include "encoding.h"

#include "lib/error.h"
#include "lib/fmt/fmt.h"
#include "lib/print.h"

using namespace lib;
using namespace serialrpc;

static void print_line(byte b, io::ReaderWriter &conn, error err) {
    String msg;
    msg += b;
    for (;;) {    
        byte b = conn.read_byte(err);
        if (err) {
            return;
        }

        msg += b;

        if (b == '\n') {
            break;
        }
    }
    fmt::printf("serialrpc raw log: %q\n", msg);
}

void ClientBase::start(error err) {
    print "starting client";
    sync::Lock lock(call_mtx);
    start_unlocked(err);
}

void ClientBase::close(error err) {
    ClientBase &c = *this;
    sync::Lock lock(c.call_mtx);

    State state = c.state.load();
again:
    switch (state) {
        case New:
        case Starting:
        case Running:
            break;
        
        case Closed:
            return;
            
        case Failing:
        case Failed:
            err("failing or failed");
            return;
    }

    if (!c.state.compare_and_swap(&state, Closed)) {
        goto again;
    }

    if (state != Running) {
        return;
    }

    c.conn->write_byte(0, err);
    c.conn->flush(err);
    if (err) {
        return;
    }

    c.input_worker.join();
    c.conn->close(err);
}

void ClientBase::start_request(uint32 rpc_id, CallData *call_data, error err) {
    // print "start_request";

    ClientBase &c = *this;
    c.start_unlocked(err);
    if (err) {
        return;
    }

    // write rpc_id
    // print "writing rpc_id", rpc_id;
    varint::write_uint32(*c.conn, rpc_id, err);
    if (err) {
        return;
    }

    // push call data
    if (c.head == nil) {
        c.head = call_data;
    }
    if (c.tail != nil) {
        c.tail->next = call_data;
    }
    c.tail = call_data;
}

void ClientBase::finish_request(error err) {
    ClientBase &c = *this;
    c.conn->flush(err);
}

void ClientBase::handle_error_response(error err) {
    String text = read_chunked(*conn, err);
    if (err) {
        fail(sync::Lock(call_mtx));
    }

    err(fmt::errorf("rpc error: %s", text));
}

void ClientBase::fail(sync::Lock const&) {
    ClientBase &c = *this;
    c.head = nil;
    c.tail = nil;

again:
    State state = c.state.load();
    if (state == Failing || state == Failed) {
        return;
    }

    if (!c.state.compare_and_swap(&state, Failing)) {
        goto again;
    }

    c.conn->close(error::ignore);
}

void ClientBase::start_unlocked(error err) {
    ClientBase &c = *this;
    State state = c.state.load();

    do {
        if (state != New) {
            return;
        }
    } while (c.state.compare_and_swap(&state, Starting));

    this->input_worker = sync::go([&] { this->input(); });

    sync::Lock lock(cond_mutex);
    while (c.state.load() == Starting) {
        this->call_cond.wait(this->cond_mutex);
    }

    print "started";

    if (c.state.load() == Failing) {
        err(*this->err);
        this->state.store(Failed);
        this->call_cond.signal();
    }
}

void ClientBase::input() {
    ClientBase &c = *this;
    print "input started";
    ErrorReporter err = [&](Error &e){
        eprint "serialrpc error: %v" % e;
        panic("!");

        sync::Lock lock(cond_mutex);

        this->err = &e;
        this->state.store(Failing);
        this->call_cond.signal();

        while (this->state.load() != Failed) {
            this->call_cond.wait(cond_mutex);
        }

        eprint "serialrpc error handling done";

        this-> err = nil;
    };

    this->client_hello(err);
    print "sent client hello";
    if (err) {
        return;
    }

    for (;;) {
        byte b = conn->read_byte(err);
        ServerMessageType type = ServerMessageType(b);
        // print "GOT MSG %2X" % (int) type;
        if (err) {
            return;
        }        

        switch (type) {
        case ServerMessageType::Reply:
        case ServerMessageType::ErrorReply:
        case ServerMessageType::Unknown:
        case ServerMessageType::TooBig:
        case ServerMessageType::BadMessage:
            handle_reply(type, err);
            continue;

        case Event: {
            uint32 event_id = varint::read_uint32(*c.conn, err);
            if (err) {
                return;
            }
            c.handle_event(event_id, err);
            if (err) {
                return;
            }
            continue;
        }

        case ServerMessageType::Log:
            handle_log(err);
            if (err) {
                return;
            }
            continue;
            
        case ServerMessageType::FatalError:
            err(ErrFatal());
            return;
            
        case ServerMessageType::ServerGoodbye:
            // print "received server goodbye";
            return;
        
        case ServerMessageType::ServerHello:
            return err("serialrpc got unexpected ServerHello");
        }
        
        if (is_printable(b)) {
            print_line(b, *conn, err);
            continue;
        }
        
        return err("serialrpc got unexpected byte 0x%2X", (int) type);
    }    
}

void ClientBase::handle_log(error err) {
    ClientBase &c = *this;

    uint32 nbytes = varint::read_uint32(*c.conn, err);
    if (err) {
        return;
    }

    // print "log nbytes", nbytes;

    // for (int i = 0; i < nbytes; i++) {
    //     char ch = c.conn->read_byte(error::panic);
    //     fmt::printf("read %d char %#U\n", i, ch);
    // }

    Buffer data(nbytes);
    io::read_full(*c.conn, data, err);
    if (err) {
        return;
    }
    
    // fmt::printf("serialrpc log: %q\n", str(data));
    if (len(data) == 0) {
        return;
    }
    if (data[len(data) - 1] == '\n') {
        fmt::printf("serialrpc log: %s", str(data));
    } else {
        fmt::printf("serialrpc log: %s\n", str(data));
    }
    
}

void ClientBase::handle_reply(ServerMessageType type, error err) {
    ClientBase &c = *this;
    // pop call data
    CallData *call_data;
    {
        sync::Lock lock(call_mtx);
        if (c.head == nil) {
            err("unexpected reply");
        }
        call_data = c.head;
        if (c.head == c.tail) {
            c.tail = nil;
        }
        c.head = c.head->next;
    }

    if (type != ServerMessageType::Reply) {
        call_data->client->handle_error_response(*call_data->err);
    } else if (call_data->unmarshal) {
        call_data->unmarshal(call_data, *c.conn);
    }

    call_data->response_received.notify();
}

void ClientBase::client_hello(error err) {
    this->conn->write_byte(byte(ClientHello), err);
    if (err) {
        return;
    }
    this->conn->flush(err);
    if (err) {
        return;
    }
    
again:
    byte b = this->conn->read_byte(err);
    if (err) {
        return;
    }

    if (is_printable(b)) {
        print_line(b, *conn, err);
        if (err) {
            return;
        }
        goto again;
    } else if (b == byte(ServerMessageType::Log)) {
        handle_log(err);
        if (err) {
            return;
        }
        goto again;        
    }

    if (b != byte(ServerHello)) {
        err("serialrpc received unexpected start byte: %#X; wanted ServerHello (%#X)", (int) b, (int) ServerHello);
        return;
    }
    
    print "serialrpc client: handshake completed";

    sync::Lock lock(cond_mutex);

    State state = this->state.load();
    do {
        if (state != Starting) {
            break;
        }
    } while (!this->state.compare_and_swap(&state, Running));

    this->call_cond.signal();
}

ClientBase::ClientBase(std::shared_ptr<lib::io::ReaderWriter> conn)
    : conn(conn) {}

void ClientBase::Waiter::notify() {

    state.store(1, std::memory_order::release);
    state.notify_one();
    state.store(2);
}

void ClientBase::Waiter::wait() {
    for (;;) {
        int s = state.load(std::memory_order::acquire);
        if (s == 0) {
        state.wait(0);
        continue;
        }
        if (s == 2) {
        return;
        }
        // spin wait
    }
}
serialrpc::ClientBase::~ClientBase() {
    this->close(error::ignore);
}
