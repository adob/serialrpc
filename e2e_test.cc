#include "lib/io/io.h"
#include "lib/io/pipe.h"
#include "lib/testing/testing.h"
#include "lib/print.h"
#include "lib/serial/serial_listener.h"
#include "example.pb_msg.h"
#include "example.pb_client.h"
#include "example.pb_server.h"

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

struct Summer : examplepb::SumService {
    examplepb::RPCServer *server = nil;

    examplepb::SumResponse sum(examplepb::SumRequest const &req, error) override {
        print "sum service got", req.left, req.right;
        return {.answer = req.left + req.right};
    }

    void subscribe_sum_events(examplepb::RPCServer &server, examplepb::SumEventsRequest const &req, error) override {
        print "SERVER GOT SUBSCRIBE";
        if (req.v != 42) {
            panic("bad req value");
        }
        if (this->server) {
            panic("server already set");
        }
        this->server = &server;
    }

    void unsubscribe_sum_events() override {
        print "SERVER GOT UNSUBSCRIBE";
        this->server = nil;
    }

    void do_event(int i) {
        server->send_SumService_sum_events({.event = i });
    }
} ;

struct CANService : examplepb::CANService {
    void send(examplepb::CANFrame const &req, lib::error) override {
        print "CANService::SEND id %v" % req.frame_id;
    }
} ;

struct ExampleService : examplepb::ExampleService {
    virtual void say_hello(lib::error /*err*/) override {
        print "say_hello()";
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

void test_e2e(testing::T &t) {
    auto [client_conn, server_side] = make_pipe();
    SerialConn server_conn(*server_side);

    Summer summer;
    CANService can;
    ExampleService example;
    examplepb::RPCServer server(summer, can, example);

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

    examplepb::RPCClient client(client_conn);

    client.start(error::panic);
    print "client started";

    print "making request...";
    examplepb::SumResponse resp = client.sum_service.sum(examplepb::SumRequest{.left = 10, .right = 20}, error::panic);;
    
    if (resp.answer != 30) {
        t.errorf("client.call got %v; want 30", resp.answer);
    }

    print "got call result", resp.answer;

    print "EVENTS BEGIN";
    std::vector<int> received_events;
    client.sum_service.subscribe_sum_events({ .v = 42 }, [&](examplepb::SumEvent const &event) {
        print "GOT EVENT", event.event;
        received_events.push_back(event.event);
    }, error::panic);

    summer.do_event(1);
    summer.do_event(2);
    summer.do_event(3);
    // END EVENT

    client.can_service.send({.frame_id = 123, .data = "hello"}, error::panic);

    client.example_service.say_hello(error::panic);

    stop.store(true);
    client.close(error::panic);

    if (received_events != std::vector<int>{1, 2, 3}) {
        t.errorf("received events got %v; want [1, 2, 3]", received_events);
    }
}
