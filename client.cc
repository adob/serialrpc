#include <initializer_list>

#include "lib/io/io.h"
#include "lib/io/util.h"
#include "lib/varint/varint.h"
#include "lib/error.h"
#include "lib/fmt/fmt.h"

#include "client.h"
#include "rpc.h"
#include "internal.h"
#include "encoding.h"
#include "serialrpc/generated/serialrpc_protocol.pb_msg.h"

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

void serialrpc::Client::register_event_callback(uint32 event_id, std::function<void(lib::io::ReaderWriter &, error)> cb) {
    Client &c = *this;
    sync::Lock lock(c.event_callbacks_mtx);
    c.event_callbacks[event_id] = cb;
}
void serialrpc::Client::unregister_event_callback(uint32 event_id) {
    Client &c = *this;
    sync::Lock lock(c.event_callbacks_mtx);
    c.event_callbacks.erase(event_id);
}

void Client::start(std::span<Stub *const> stubs,
                   std::shared_ptr<Client> const &shared_client, error err) {
  Client &c = *this;
  sync::Lock lock(call_mtx);

  c.start_unlocked(stubs, shared_client, err);
}

void Client::wait(error err) {
    Client &c = *this;

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

void Client::close(error err) {
    Client &c = *this;
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

void Client::start_request(uint32 rpc_id, CallData *call_data, error err) {
    Client &c = *this;

    // write rpc_id
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

void Client::finish_request(error err) {
    Client &c = *this;
    reqnum.add(1);
    c.conn->flush(err);
}

void Client::handle_error_response(CallData const &call_data, error err) {
    Client &c = *this;
    String text = read_chunked(*c.conn, err);
    if (err) {
        //c.fail(sync::Lock(call_mtx));
        return;
    }

    (*call_data.err)(ErrReply(call_data.service_name, call_data.procedure_name, text));
}

void Client::fail(sync::Lock const&) {
    Client &c = *this;
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

void serialrpc::Client::handle_event(uint32 event_id, error err) {
    Client &c = *this;

    std::function<void(lib::io::ReaderWriter &, error)> cb;
    {
        sync::Lock lock(c.event_callbacks_mtx);
        auto it = c.event_callbacks.find(event_id);
        if (it == c.event_callbacks.end()) {
            return err("got event with unknown event_id %v", event_id);
        }
        cb = it->second;
    }

    cb(*c.conn, err);
}

void Client::start_unlocked(std::span<Stub *const> stubs, std::shared_ptr<Client> const &shared_client, error err) {
  Client &c = *this;
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

    c.input_worker = sync::go([&] { this->input(stubs, shared_client); });

    sync::Lock lock(cond_mutex);
    while (c.state.load() == Starting) {
        this->call_cond.wait(this->cond_mutex);
    }

    if (c.state.load() == Failing) {
        err(*this->err);
        this->state.store(Failed);
        this->call_cond.signal();
    }
}

void Client::input(std::span<Stub *const> stubs, std::shared_ptr<Client> const &shared_client) {
    Client &c = *this;
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

    this->client_hello(stubs, shared_client, err);
    if (err) {
        return;
    }

    for (;;) {
        byte b = conn->read_byte(err);
        ServerMessageType type = ServerMessageType(b);
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
            if (err) {
                return;
            }
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

void Client::handle_log(error err) {
    Client &c = *this;

    uint32 nbytes = varint::read_uint32(*c.conn, err);
    if (err) {
        return;
    }

    if (nbytes > MaxStringSize) {
        err("log message too big");
        return;
    }

    Buffer data(nbytes);
    io::read_full(*c.conn, data, err);
    if (err) {
        return;
    }
    
    if (len(data) == 0) {
        return;
    }
    if (data[len(data) - 1] == '\n') {
        fmt::fprintf(os::stderr, "serialrpc log: %s", str(data));
    } else {
        fmt::fprintf(os::stderr, "serialrpc log: %s\n", str(data));
    }
}

void Client::handle_reply(ServerMessageType type, error err) {
    Client &c = *this;
    // pop call data
    CallData *call_data;
    respnum.add(1);
    {
        sync::Lock lock(call_mtx);
        if (c.head == nil) {
            err("unexpected reply");
            return;
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

void Client::client_hello(std::span<Stub *const> stubs, std::shared_ptr<Client> const &shared_client, error err) {
    Client &c = *this;
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
        err("serialrpc: received unexpected start byte: %#X; wanted ServerHello (%#X)", (int) b, (int) ServerHello);
        return;
    }

    c.read_services_def(stubs, shared_client, err);
    if (err) {
        return;
    }
    
    sync::Lock lock(cond_mutex);

    State state = this->state.load();
    do {
        if (state != Starting) {
            break;
        }
    } while (!this->state.compare_and_swap(&state, Running));

    this->call_cond.signal();
}

void Client::read_services_def(std::span<Stub *const> stubs, std::shared_ptr<Client> const &shared_client, error err) {
    Client &c = *this;
    io::ReaderWriter &conn = *c.conn;

    int offset = 1;
    for (;;) {
        Tag tag = read_tag(conn, err);
        if (err) {
            return;
        }
        if (tag.type == Tag::End) {
            break;
        }
        switch (tag.field_num) {
        case serialrpcpb::ServerHello::ServicesFieldNumber: {
            serialrpcpb::ServiceDef service_def = serialrpcpb::ServiceDef::unmarshal(conn, err, MaxNesting);
            if (err) {
                return;
            }
            c.handle_service_def(service_def, stubs, shared_client, offset, err);
            if (err) {
                return;
            }
            offset += service_def.num_endpoints;
            break;
        }

        case serialrpcpb::ServerHello::ProtocolVersionFieldNumber: {
            uint32 protocol_version = varint::read_uint32(conn, err);
            if (err) {
                return;
            }
            if (protocol_version != ProtocolVersion) {
                err("serialrpc: protocol version mismatch: server has %d; client has %d", protocol_version, ProtocolVersion);
                return;
            }
            break;
        }

        default:
            serialrpc::skip(conn, tag.type, err, MaxNesting);
            if (err) {
                return;
            }
        }
    }
}

void Client::handle_service_def(
    serialrpcpb::ServiceDef const &service_def, 
    std::span<Stub *const> stubs, 
    std::shared_ptr<Client> const &client, 
    int offset, 
    error err) {
    
    for (Stub *const stub : stubs) {
        if (str(stub->uuid) != str(service_def.uuid)) {
            continue;
        }

        if (stub->major_version != service_def.major_version) {
            err("serialrpc: server major version mismatch for service %q: server has %d; client has %d", 
                stub->name, service_def.major_version, stub->major_version);
            return;
        }

        if (stub->minor_version > service_def.minor_version) {
            err("serialrpc: server minor version mismatch too low for service %q: server has %d; client has %d", 
                stub->name, service_def.minor_version, stub->minor_version);
            return;
        }

        stub->client = client;
        stub->rpc_offset = offset;
            
        return;
    }
    
}

Client::Client(std::shared_ptr<lib::io::ReaderWriter> const &conn)
    : conn(conn) {}

void Client::Waiter::notify() {

    state.store(1, std::memory_order::release);
    state.notify_one();
    state.store(2);
}

void Client::Waiter::wait() {
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
serialrpc::Client::~Client() {
    this->close(error::ignore);
}

void serialrpc::Client::init(std::shared_ptr<lib::io::ReaderWriter> const &conn) {
    Client &c = *this;
    sync::Lock lock(c.call_mtx);

    if (c.conn != nil) {
        panic("already initialized");
    }

    c.conn = conn;
}

std::shared_ptr<Client> serialrpc::connect(std::shared_ptr<lib::io::ReaderWriter> const &conn, std::initializer_list<Stub*> stubs, error err) {
    std::shared_ptr<Client> client = std::make_shared<Client>(conn);

    Client &c = *client;
    std::span<Stub *const> stubs_span{stubs.begin(), stubs.size()};
    c.start(stubs_span, client, err);

    for (Stub *const stub : stubs) {
        if (stub->client.get() != client.get()) {
            err("serialrpc: server does not have expected service %q (uuid %s)", stub->name, format_uuid(stub->uuid));
            return nil;
        }
    }
    return client;
}
