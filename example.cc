#include "lib/base.h"

#include "lib/io/io.h"
#include "lib/io/pipe.h"
#include "lib/print.h"
#include "lib/serial/serial_listener.h"
#include "lib/sync/lock.h"
#include "lib/time/time.h"
#include "lib/varint/varint.h"

#include "generated/example.pb_msg.h"
#include "generated/example.pb_client.h"
#include "generated/example.pb_server.h"
#include "generated/serialrpc_protocol.pb_msg.h"
#include <numeric>


using namespace lib;

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
        print "PipeEnd::close()";
        this->r->close(err);
        print "L48";
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
    std::function<void(examplepb::SumEvent const &)> sum_event_callback;

    io::ReaderWriter *conn = nil;

    examplepb::SumResponse sum(examplepb::SumRequest const &req, error) override {
        conn->write("\xff", error::panic);
        return {.answer = req.left + req.right};
    }

    void subscribe_sum_events(examplepb::SumEventsRequest const &req, std::function<void(examplepb::SumEvent const &)> const &callback, error) override {

    }

    void unsubscribe_sum_events(lib::error) override {
    }
} ;


int main() {
    debug::init();
    auto [client_conn, server_side] = make_pipe();
    SerialConn server_conn(*server_side);

    Summer summer;
    serialrpc::Server server(summer);

    sync::go g1 = [&]{
        server.accept(server_conn, error::log);
    };

    examplepb::SumServiceStub sum_stub;
    auto client = serialrpc::connect(client_conn, "<file>", {&sum_stub}, error::panic);

    auto err_handler = [](Error const &e) {
        print e;
    };

    summer.conn = server_side.get();
    examplepb::SumResponse resp = sum_stub.sum(examplepb::SumRequest{.left = 10, .right = 20}, err_handler);
    print "got response", resp.answer;

    print "calling close()...";
    client->close(error::panic);
}
