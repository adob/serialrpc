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

static sync::atomic<int> reqnum = 0;
static sync::atomic<int> respnum = 0;

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
    fmt::fprintf(os::stderr, "serialrpc raw log: %q\n", msg);
}

void ClientBase::start(error err) {
    sync::Lock lock(call_mtx);
    start_unlocked(err);
}

void ClientBase::wait(error err) {
    ClientBase &c = *this;

    // c.input_worker.join();

    sync::Lock lock(cond_mutex);
    for (;;) {
        State state = c.state.load();
        if (state == Closed || state == Failing || state == Failed) {
            break;
        }
        this->call_cond.wait(this->cond_mutex);
    }

    if (c.state.load() == Failing) {
        err(*this->err);
        this->state.store(Failed);
        this->call_cond.signal();
    }
}

void ClientBase::close(error err) {
    ClientBase &c = *this;
    sync::Lock lock(c.call_mtx);

    if (c.conn == nil) {
        return;
    }

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
            err(*this->err);
            this->state.store(Failed);
            return;

        case Failed:
            err("failed");
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
    reqnum.add(1);
    c.conn->flush(err);
}

void ClientBase::handle_error_response(CallData const &call_data, error err) {
    ClientBase &c = *this;
    String text = read_chunked(*c.conn, err);
    if (err) {
        //c.fail(sync::Lock(call_mtx));
        return;
    }

    (*call_data.err)(ErrReply(call_data.service_name, call_data.procedure_name, text));
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
again:
    State state = c.state.load();

    switch (state) {
    case New:
        if (!c.state.compare_and_swap(&state, Starting)) {
            goto again;
        }
        break;

    case Starting: {
        sync::Lock lock(cond_mutex);
        while (c.state.load() == Starting) {
            this->call_cond.wait(this->cond_mutex);
        }
        goto again;
    }
    
    case Running:
        return;
        
    case Closed:
        err("closed");
        return;

    case Failing: {
        sync::Lock lock(cond_mutex);
        if (c.state.load() != Failing) {
            goto again;
        }

        err(*this->err);
        this->state.store(Failed);
        this->call_cond.signal();
        return;
    }
    
    case Failed:
        err("failed");
        return;
    }

    this->input_worker = sync::go([&] { this->input(); });

    sync::Lock lock(cond_mutex);
    while (c.state.load() == Starting) {
        this->call_cond.wait(this->cond_mutex);
    }

    // eprint "serialrpc started";

    if (c.state.load() == Failing) {
        err(*this->err);
        this->state.store(Failed);
        this->call_cond.signal();
    }
}

void ClientBase::input() {
    ClientBase &c = *this;
    ErrorFunc err = [&](Error &e){
        sync::Lock lock(cond_mutex);

        this->err = &e;
        this->state.store(Failing);
        this->call_cond.signal();

        while (c.head != nil) {
            (*c.head->err)(e);
            c.head->response_received.notify();
            c.head = c.head->next;
        }

        while (this->state.load() != Failed) {
            this->call_cond.wait(cond_mutex);
        }

        this->err = nil;
    };

    this->client_hello(err);
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
        fmt::fprintf(os::stderr, "serialrpc log: %s", str(data));
    } else {
        fmt::fprintf(os::stderr, "serialrpc log: %s\n", str(data));
    }
    
}

void ClientBase::handle_reply(ServerMessageType type, error err) {
    ClientBase &c = *this;
    // pop call data
    CallData *call_data;
    respnum.add(1);
    {
        sync::Lock lock(call_mtx);
        if (c.head == nil) {
            eprint "unexpected reply: reqnum %v; respnum %v" % reqnum.load(), respnum.load();
            err("unexpected reply");
        }
        call_data = c.head;
        if (c.head == c.tail) {
            c.tail = nil;
        }
        c.head = c.head->next;
    }

    if (type != ServerMessageType::Reply) {
        call_data->client->handle_error_response(*call_data, err);
    } else if (call_data->unmarshal) {
        call_data->unmarshal(call_data, *c.conn, err);
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
    
    eprint "serialrpc client: handshake completed";

    sync::Lock lock(cond_mutex);

    State state = this->state.load();
    do {
        if (state != Starting) {
            break;
        }
    } while (!this->state.compare_and_swap(&state, Running));

    this->call_cond.signal();
}

ClientBase::ClientBase(std::shared_ptr<lib::io::ReaderWriter> const &conn)
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
void serialrpc::ClientBase::init(std::shared_ptr<lib::io::ReaderWriter> const &conn) {
    ClientBase &c = *this;
    sync::Lock lock(c.call_mtx);

    if (c.conn != nil) {
        panic("already initialized");
    }

    c.conn = conn;
}
