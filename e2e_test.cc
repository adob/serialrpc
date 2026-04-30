#include "lib/io/io.h"
#include "lib/io/pipe.h"
#include "lib/testing/testing.h"
#include "lib/print.h"
#include "lib/serial/serial_listener.h"
#include "lib/sync/lock.h"
#include "lib/time/time.h"
#include "lib/varint/varint.h"

#include "generated/example.pb_msg.h"
#include "generated/example.pb_client.h"
#include "generated/example.pb_server.h"
#include "generated/serialrpc_protocol.pb_msg.h"

#include <memory>

using namespace lib;
using namespace serialrpc;

struct PipeEnd : io::ReaderWriter {
    std::shared_ptr<io::Writer> w;
    std::shared_ptr<io::Reader> r;

    virtual io::ReadResult direct_read(buf bytes, error err) override {
        return this->r->direct_read(bytes, err);
    }

    virtual size direct_write(str data, error err) override {
        return this->w->direct_write(data, err);
    }

    void close(error err) override {
        this->r->close(err);
        this->w->close(err);
    }
} ;

struct PipePair {
    std::shared_ptr<PipeEnd> p1, p2;
} ;

PipePair make_pipe() {
    PipePair p;

    auto [r1, w1] = io::pipe();
    auto [r2, w2] = io::pipe();

    p.p1 = std::make_shared<PipeEnd>();
    p.p2 = std::make_shared<PipeEnd>();

    p.p1->r = r1;
    p.p2->w = w1;
    p.p1->w = w2;
    p.p2->r = r2;

    return p;
}

struct Summer : examplepb::SumServiceBase {
    std::function<void(examplepb::SumEvent const &)> sum_event_callback;;

    examplepb::SumResponse sum(examplepb::SumRequest const &req, error) override {
        print "sum service got", req.left, req.right;
        return {.answer = req.left + req.right};
    }

    void subscribe_sum_events(examplepb::SumEventsRequest const &req, std::function<void(examplepb::SumEvent const &)> const &callback, error) override {
        print "SERVER GOT SUBSCRIBE";
        if (req.v != 42) {
            panic("bad req value");
        }
        if (this->sum_event_callback) {
            panic("callback already set");
        }
        this->sum_event_callback = callback;
    }

    void unsubscribe_sum_events(lib::error) override {
        print "SERVER GOT UNSUBSCRIBE";
        this->sum_event_callback = nullptr;
    }

    void do_event(int i) {
        if (this->sum_event_callback) {
            this->sum_event_callback(examplepb::SumEvent{.event = i});
        }
    }
} ;

struct BlockingSummer : examplepb::SumServiceBase {
    sync::Mutex mtx;
    sync::Cond cond;
    bool request_received = false;
    bool released = false;

    examplepb::SumResponse sum(examplepb::SumRequest const &, error) override {
        sync::Lock lock(mtx);
        request_received = true;
        cond.broadcast();
        while (!released) {
            cond.wait(mtx);
        }
        return {};
    }

    void wait_for_request() {
        sync::Lock lock(mtx);
        while (!request_received) {
            cond.wait(mtx);
        }
    }

    void release() {
        sync::Lock lock(mtx);
        released = true;
        cond.broadcast();
    }

    void subscribe_sum_events(examplepb::SumEventsRequest const &, std::function<void(examplepb::SumEvent const &)> const &, error) override {}

    void unsubscribe_sum_events(lib::error) override {}
} ;

struct CANService : examplepb::CANServiceBase {
    void send(examplepb::CANFrame const &req, lib::error) override {
        print "CANService::SEND id %v" % req.frame_id;
    }
} ;

struct ExampleService : examplepb::ExampleServiceBase {
    virtual void say_hello(lib::error /*err*/) override {
        print "say_hello()";
    }

    std::function<void()> event1_callback;
    std::function<void(examplepb::ExampleEvent const&)> event2_callback;
    std::function<void()> event3_callback;

    virtual void subscribe_example_event1(std::function<void()> const &cb, lib::error err) override {
        event1_callback = cb;
    }
    virtual void unsubscribe_example_event1(lib::error err) override {
        event1_callback = nullptr;
    }
    virtual void subscribe_example_event2(std::function<void(examplepb::ExampleEvent const&)> const &cb, lib::error err) override {
        event2_callback = cb;
    }
    virtual void unsubscribe_example_event2(lib::error err) override {
        event2_callback = nullptr;
    }
    virtual void subscribe_example_event3(examplepb::ExampleEvent const &req, std::function<void()> const &cb, lib::error err) override {
        event3_callback = cb;
    }
    virtual void unsubscribe_example_event3(lib::error err) override {
        event3_callback = nullptr;
    }
} ;

struct SerialConn : serial::Conn {
    io::ReaderWriter &fwd;
    SerialConn(io::ReaderWriter &fwd) : fwd(fwd) {}

    io::ReadResult direct_read(buf bytes, error err) override {
        return fwd.read(bytes, err);
    }

    size direct_write(str data, error err) override {
        return fwd.write(data, err);
    }
} ;

void test_bare_unknown_reply_fails_connection_and_wakes_call(testing::T &t) {
    auto [client_conn, server_side] = make_pipe();

    sync::go server = [&] {
        byte hello = server_side->read_byte(error::panic);
        if (hello != byte(ClientHello)) {
            panic("bad client hello");
        }

        server_side->write_byte(byte(ServerHello), error::panic);
        write_tag(*server_side, serialrpcpb::ServerHello::ProtocolVersionFieldNumber, Tag::VarInt, error::panic);
        varint::write_uint32(*server_side, ProtocolVersion, error::panic);

        serialrpcpb::ServiceDef service_def = to_service_def<examplepb::SumServiceBase>();
        Stack stack;
        marshal_field(*server_side, serialrpcpb::ServerHello::ServicesFieldNumber, service_def, error::panic, MaxNesting, stack);
        server_side->write_byte(byte(Tag::End), error::panic);
        server_side->flush(error::panic);

        uint32 rpc_id = varint::read_uint32(*server_side, error::panic);
        if (rpc_id != 1) {
            panic("bad rpc id");
        }
        (void) unmarshal<examplepb::SumRequest>(*server_side, error::panic);

        server_side->write_byte(byte(ServerMessageType::Unknown), error::panic);
        server_side->flush(error::panic);
    };

    examplepb::SumServiceStub sum_stub;
    std::shared_ptr<Client> client = serialrpc::connect(client_conn, "<test connection>", {&sum_stub}, error::panic);

    ErrorRecorder call_err;
    sync::go call = [&] {
        error call_error = call_err;
        (void) sum_stub.sum(examplepb::SumRequest{.left = 10, .right = 20}, call_error);
    };

    call.join();

    ErrorRecorder session_err;
    client->wait(session_err);

    if (!call_err) {
        t.errorf("sum() did not receive an error for bare Unknown reply");
    }
    if (!session_err) {
        t.errorf("client session did not fail after bare Unknown reply");
    }
}

void test_call_mtx_deadlock(testing::T &t) {
    auto [client_conn, server_side] = make_pipe();
    SerialConn server_conn(*server_side);

    Summer summer;
    serialrpc::Server server(summer);

    sync::go server_thread = [&] {
        server.accept(server_conn, error::panic);
    };

    examplepb::SumServiceStub sum_stub;
    std::shared_ptr<Client> client = serialrpc::connect(client_conn, "<test connection>", {&sum_stub}, error::panic);

    sync::atomic<bool> call_done = false;
    ErrorRecorder call_err;
    sync::go call = [&] {
        (void) sum_stub.sum(examplepb::SumRequest{.left = 10, .right = 20}, call_err);
        call_done.store(true);
    };

    ErrorRecorder close_err;
    sync::go close_thread = [&] {
        client->close(close_err);
    };

    close_thread.join();
    call.join();

    server_thread.join();

    if (close_err) {
        t.errorf("close() got error %v; want nil", close_err);
    }
    if (call_err) {
        t.errorf("call got error %v; want nil", call_err);
    }
}
 
void test_unsolicited_server_goodbye_wakes_pending_call(testing::T &t) {
    auto [client_conn, server_side] = make_pipe();
    SerialConn server_conn(*server_side);

    BlockingSummer summer;
    serialrpc::Server server(summer);

    sync::go server_thread = [&] {
        server.accept(server_conn, error::ignore);
    };

    examplepb::SumServiceStub sum_stub;
    std::shared_ptr<Client> client = serialrpc::connect(client_conn, "<test connection>", {&sum_stub}, error::panic);

    sync::atomic<bool> call_done = false;
    ErrorRecorder call_err;
    sync::go call = [&] {
        (void) sum_stub.sum(examplepb::SumRequest{.left = 10, .right = 20}, call_err);
        call_done.store(true);
    };

    summer.wait_for_request();

    server.send_goodbye(server_conn,error::panic);
    call.join();

    summer.release();
    server_side->close(error::ignore);
    server_thread.join();

    if (!call_err) {
        t.errorf("pending call did not receive an error after unsolicited ServerGoodbye");
    }
}

void test_unsolicited_server_goodbye_without_pending_calls_fails_session(testing::T &t) {
    auto [client_conn, server_side] = make_pipe();
    SerialConn server_conn(*server_side);

    Summer summer;
    serialrpc::Server server(summer);

    sync::go server_thread = [&] {
        server.accept(server_conn, error::ignore);
    };

    examplepb::SumServiceStub sum_stub;
    std::shared_ptr<Client> client = serialrpc::connect(client_conn, "<test connection>", {&sum_stub}, error::panic);

    server.send_goodbye(server_conn,error::panic);

    ErrorRecorder session_err;
    client->wait(session_err);

    server_side->close(error::ignore);
    server_thread.join();

    if (!session_err) {
        t.errorf("client session did not receive an error after unsolicited ServerGoodbye");
    } else if (session_err.msg != "serialrpc on \"<test connection>\": received unsolicited ServerGoodbye") {
        t.errorf("client session got error %q after unsolicited ServerGoodbye; want ErrUnsolicitedServerGoodbye", session_err);
    }
}

void test_call_after_server_goodbye_fails_without_hanging(testing::T &t) {
    auto [client_conn, server_side] = make_pipe();
    SerialConn server_conn(*server_side);

    Summer summer;
    serialrpc::Server server(summer);

    sync::go server_thread = [&] {
        server.accept(server_conn, error::ignore);
    };

    auto sum_stub = std::make_shared<examplepb::SumServiceStub>();
    std::shared_ptr<Client> client = serialrpc::connect(client_conn, "<test connection>", {sum_stub.get()}, error::panic);

    server.send_goodbye(server_conn,error::panic);
    client->wait(error::ignore);

    sync::atomic<bool> call_done = false;
    auto call_err = std::make_shared<ErrorRecorder>();
    sync::go call = [sum_stub, call_err, &call_done] {
        (void) sum_stub->sum(examplepb::SumRequest{.left = 10, .right = 20}, *call_err);
        call_done.store(true);
    };

    for (int i = 0; i < 100 && !call_done.load(); i++) {
        time::sleep(time::millisecond);
    }

    server_side->close(error::ignore);
    server_thread.join();

    if (call_done.load()) {
        call.join();
    } else {
        call.detach();
        t.errorf("call made after ServerGoodbye did not return");
        return;
    }

    if (!*call_err) {
        t.errorf("call made after ServerGoodbye did not receive an error");
    }
}

void test_e2e(testing::T &t) {
    auto [client_conn, server_side] = make_pipe();
    SerialConn server_conn(*server_side);

    Summer summer;
    CANService can;
    ExampleService example;
    serialrpc::Server server(summer, can, example);

    sync::atomic<bool> stop = false;

    sync::go g1 = [&]{
        for (;;) {
            if (stop.load()) {
                print "server is done";
                return;
            }
            server.accept(server_conn, error::panic);
        }
    };

    examplepb::SumServiceStub sum_stub;
    examplepb::CANServiceStub can_stub;
    examplepb::ExampleServiceStub example_stub;
    std::shared_ptr<Client> client = serialrpc::connect(client_conn, "<test connection>", {&sum_stub, &can_stub, &example_stub}, error::panic);
    print "client connected";

    print "client started";

    print "making request...";
    examplepb::SumResponse resp = sum_stub.sum(examplepb::SumRequest{.left = 10, .right = 20}, error::panic);;
    
    print "got response", resp.answer;
    if (resp.answer != 30) {
        t.errorf("client.call got %v; want 30", resp.answer);
    }

    print "got call result", resp.answer;

    print "EVENTS BEGIN";
    std::vector<int> received_events;
    sum_stub.subscribe_sum_events({ .v = 42 }, [&](examplepb::SumEvent const &event) {
        print "GOT EVENT", event.event;
        received_events.push_back(event.event);
    }, error::panic);

    summer.do_event(1);
    summer.do_event(2);
    summer.do_event(3);
    // END EVENT

    int event1_count = 0;
    int event2_count = 0;
    int event3_count = 0;

        // More event tests
    example_stub.subscribe_example_event1([&]{
        event1_count++;
    }, error::panic);
    example_stub.subscribe_example_event2([&](examplepb::ExampleEvent const &event){
        event2_count++;
    }, error::panic);
    example_stub.subscribe_example_event3({}, [&]{
        event3_count++;
    }, error::panic);

    example.event1_callback();
    example.event2_callback(examplepb::ExampleEvent{});
    example.event3_callback();

    // other tests

    can_stub.send({.frame_id = 123, .data = "hello"}, error::panic);

    example_stub.say_hello(error::panic);

    stop.store(true);
    client->close(error::panic);

    if (received_events != std::vector<int>{1, 2, 3}) {
        t.errorf("received events got %v; want [1, 2, 3]", received_events);
    }
    if (event1_count != 1) {
        t.errorf("event1_count got %v; want 1", event1_count);
    }
    if (event2_count != 1) {
        t.errorf("event2_count got %v; want 1", event2_count);
    }
    if (event3_count != 1) {
        t.errorf("event3_count got %v; want 1", event3_count);
    }
}
