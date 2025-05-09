#include "lib/io/io.h"
#include "lib/io/pipe.h"
#include "lib/testing/testing.h"
#include "lib/print.h"
#include "serial/serial_listener.h"
#include "example.pb.h"

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

struct Summer : SumService {
    RPCServer *server = nil;

    SumResponse sum(SumRequest const &req, error) override {
        print "sum service got", req.left, req.right;
        return {.answer = req.left + req.right};
    }

    void subscribe_sum_events(RPCServer &server, SumEventsRequest const &req) override {
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
    RPCServer server(summer);

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

    RPCClient client(client_conn);

    client.start(error::panic);
    print "client started";

    print "making request...";
    SumResponse resp = client.sum_service.sum(SumRequest{.left = 10, .right = 20}, error::panic);;
    
    if (resp.answer != 30) {
        t.errorf("client.call got %v; want 30", resp.answer);
    }

    print "got call result", resp.answer;

    print "EVENTS BEGIN";
    std::vector<int> received_events;
    client.sum_service.subscribe_sum_events({ .v = 42 }, [&](SumEvent const &event) {
        print "GOT EVENT", event.event;
        received_events.push_back(event.event);
    }, error::panic);

    summer.do_event(1);
    summer.do_event(2);
    summer.do_event(3);
    // END EVENT

    stop.store(true);
    client.close(error::panic);

    if (received_events != std::vector<int>{1, 2, 3}) {
        t.errorf("received events got %v; want [1, 2, 3]", received_events);
    }
}