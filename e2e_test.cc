// Parse external headers before importing baselib's standard-library header units.
#include <memory>
#include <string_view>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/text_format.h>

import lib.io;
import lib.testing;
#include "lib/print.h"
import lib.serial.serial_listener;
import lib.sync.lock;
import lib.time;
import lib.varint;

#include "generated/example.pb_msg.h"
#include "generated/example.pb_client.h"
#include "generated/example.pb_server.h"
#include "generated/serialrpc_protocol.pb_client.h"
#include "generated/serialrpc_protocol.pb_msg.h"
#include "cmd/serialrpc/call.h"


using namespace lib;
using namespace serialrpc;

void test_generated_service_method_table(testing::T &t) {
    auto const& service = examplepb::SumService::Info;
    if (service.name != "SumService"
        || service.package != "example"
        || service.uuid != std::array<uint8_t, 16>{
            0x4f, 0x90, 0xfb, 0x19, 0x7b, 0x58, 0x47, 0x55,
            0xbb, 0x1f, 0x3c, 0x81, 0xdc, 0x0a, 0x6d, 0x4d}
        || service.major_version != 0
        || service.minor_version != 0
        || service.num_endpoints != 2) {
        t.errorf("SumService generated incorrect service metadata");
    }

    auto const& methods = examplepb::SumService::Methods;

    if (methods.size() != 2) {
        t.errorf("SumService has %v methods; want 2", methods.size());
        return;
    }
    if (methods[0].name != "sum" || methods[0].id != 1) {
        t.errorf("SumService method 0 is {%q, %v}; want {sum, 1}",
            methods[0].name, methods[0].id);
    }
    if (methods[1].name != "sum_events" || methods[1].id != 2) {
        t.errorf("SumService method 1 is {%q, %v}; want {sum_events, 2}",
            methods[1].name, methods[1].id);
    }
}

void test_generated_method_schema(testing::T &t) {
    auto const &method_metadata = examplepb::SumService::Methods[0];
    if (method_metadata.requestType != &examplepb::SumRequest::Info
        || method_metadata.responseType != &examplepb::SumResponse::Info
        || method_metadata.requestType->name != "example.SumRequest"
        || method_metadata.responseType->name != "example.SumResponse"
        || len(method_metadata.requestType->fields) != 2
        || method_metadata.requestType->fields.begin()->name != "left"
        || method_metadata.requestType->fields.begin()->type != &TypeInt32) {
        t.errorf("generated method schema does not describe SumService.sum");
        return;
    }

    auto const &send_metadata = examplepb::CANService::Methods[0];
    if (send_metadata.responseType != &TypeVoid
        || examplepb::CANService::Types[1] != &TypeVoid) {
        t.errorf("generated method schema does not use the shared void type");
        return;
    }

    serialrpcpb::ServiceInfo service_info;
    serialrpcpb::TypeInfo request_info;
    request_info.id = 1;
    request_info.type = serialrpcpb::FieldType::FieldTypeMessage;
    request_info.name = "example.SumRequest";
    request_info.fields.push_back({
        .name = "left", .number = 1,
        .type = serialrpcpb::FieldType::FieldTypeInt32});
    request_info.fields.push_back({
        .name = "right", .number = 2,
        .type = serialrpcpb::FieldType::FieldTypeInt32});
    request_info.fields.push_back({
        .name = "first_mode", .number = 3,
        .type = serialrpcpb::FieldType::FieldTypeEnum, .type_id = 3});
    request_info.fields.push_back({
        .name = "second_mode", .number = 4,
        .type = serialrpcpb::FieldType::FieldTypeEnum, .type_id = 4});
    service_info.types.push_back(request_info);

    serialrpcpb::TypeInfo response_info;
    response_info.id = 2;
    response_info.type = serialrpcpb::FieldType::FieldTypeMessage;
    response_info.name = "example.SumResponse";
    response_info.fields.push_back({
        .name = "answer", .number = 1,
        .type = serialrpcpb::FieldType::FieldTypeInt32});
    service_info.types.push_back(response_info);

    serialrpcpb::TypeInfo first_enum;
    first_enum.id = 3;
    first_enum.type = serialrpcpb::FieldType::FieldTypeEnum;
    first_enum.name = "example.FirstMode";
    first_enum.enum_values.push_back({.name = "Unknown", .number = 0});
    first_enum.enum_values.push_back({.name = "Default", .number = 0});
    first_enum.enum_values.push_back({.name = "Enabled", .number = 1});
    service_info.types.push_back(first_enum);

    serialrpcpb::TypeInfo second_enum;
    second_enum.id = 4;
    second_enum.type = serialrpcpb::FieldType::FieldTypeEnum;
    second_enum.name = "example.SecondMode";
    second_enum.enum_values.push_back({.name = "Unknown", .number = 0});
    second_enum.enum_values.push_back({.name = "Disabled", .number = 1});
    service_info.types.push_back(second_enum);

    serialrpcpb::MethodInfo method_info;
    method_info.name = "sum";
    method_info.id = 1;
    method_info.request_type = 1;
    method_info.response_type = 2;

    google::protobuf::DescriptorPool pool(
        google::protobuf::DescriptorPool::generated_pool());
    auto method = serialrpc::cli::detail::build_method_descriptor(
        service_info, method_info, pool, error::panic);
    if (method == nullptr) {
        t.errorf("could not build a dynamic method descriptor");
        return;
    }
    auto const *first_mode = method->input_type()->FindFieldByName("first_mode");
    auto const *second_mode = method->input_type()->FindFieldByName("second_mode");
    if (first_mode == nullptr || second_mode == nullptr
        || first_mode->enum_type() == second_mode->enum_type()
        || first_mode->enum_type()->FindValueByName("Unknown") == nullptr
        || second_mode->enum_type()->FindValueByName("Unknown") == nullptr
        || !first_mode->enum_type()->options().allow_alias()) {
        t.errorf("dynamic schema does not preserve enum scopes and aliases");
        return;
    }

    google::protobuf::DynamicMessageFactory factory(&pool);
    auto request = std::unique_ptr<google::protobuf::Message>(
        factory.GetPrototype(method->input_type())->New());
    if (!google::protobuf::TextFormat::ParseFromString(
            "left: 10 right: 20", request.get())) {
        t.errorf("could not parse dynamic SumRequest");
        return;
    }

    io::Buffer request_buffer;
    serialrpc::cli::detail::marshal(
        *request, request_buffer, error::panic);
    auto typed_request = unmarshal<examplepb::SumRequest>(
        request_buffer, error::panic);
    if (typed_request.left != 10 || typed_request.right != 20) {
        t.errorf("dynamic request encoding got {%v, %v}; want {10, 20}",
                 typed_request.left, typed_request.right);
    }

    io::Buffer response_buffer;
    marshal(response_buffer, examplepb::SumResponse{.answer = 30},
            error::panic);
    auto response = std::unique_ptr<google::protobuf::Message>(
        factory.GetPrototype(method->output_type())->New());
    serialrpc::cli::detail::unmarshal(
        *response, response_buffer, error::panic);
    auto answer = response->GetReflection()->GetInt32(
        *response, response->GetDescriptor()->FindFieldByName("answer"));
    if (answer != 30) {
        t.errorf("dynamic response decoding got %v; want 30", answer);
    }
}

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

        serialrpcpb::ServiceDef service_def =
            to_service_def(examplepb::SumServiceBase::Info);
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
    if (!call_err.is<ErrReply>()) {
        t.errorf("sum() error has type %v; want ErrReply", call_err.type);
    }
    if (!session_err) {
        t.errorf("client session did not fail after bare Unknown reply");
    }
}

void test_call_mtx_deadlock(testing::T &t) {
    auto [client_conn, server_side] = make_pipe();
    SerialConn server_conn(*server_side);

    BlockingSummer summer;
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

    // Establish that the call is in flight before close() begins.
    summer.wait_for_request();

    ErrorRecorder close_err;
    sync::go close_thread = [&] {
        client->close(close_err);
    };

    summer.release();
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
    static_assert(decltype(server)::dispatch_table.size() == 8);

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
    serialrpcpb::DiscoveryServiceStub discovery_stub;
    std::shared_ptr<Client> client = serialrpc::connect(
        client_conn,
        "<test connection>",
        {&discovery_stub, &sum_stub, &can_stub, &example_stub},
        error::panic);
    print "client connected";

    print "client started";

    serialrpcpb::ListServicesResponse services =
        discovery_stub.list_services({}, error::panic);
    if (services.services.size() != 4) {
        t.errorf("discovery returned %v services; want 4", services.services.size());
    } else {
        std::array<std::string_view, 4> expected_names = {
            "serialrpc.DiscoveryService",
            "example.SumService",
            "example.CANService",
            "example.ExampleService",
        };
        for (size_t i = 0; i < expected_names.size(); ++i) {
            auto const& service = services.services[i];
            str service_name = service.name;
            std::string_view name(service_name.data, service_name.len);
            if (name != expected_names[i]) {
                t.errorf("discovery service %v is %q; want %q",
                    i, service_name, expected_names[i]);
            }
        }

        auto const& sum_service = services.services[1];
        if (sum_service.methods.size() != 2) {
            t.errorf("discovery returned %v SumService methods; want 2",
                sum_service.methods.size());
        } else {
            str first_method_name = sum_service.methods[0].name;
            str second_method_name = sum_service.methods[1].name;
            if (std::string_view(
                first_method_name.data,
                first_method_name.len) != std::string_view("sum")
                || sum_service.methods[0].id != 1
                || sum_service.methods[0].request_type != 0
                || std::string_view(
                second_method_name.data,
                second_method_name.len) != std::string_view("sum_events")
                || sum_service.methods[1].id != 2) {
                t.errorf("discovery returned incorrect SumService method metadata");
            }
        }
    }

    serialrpcpb::ListServicesResponse full_services =
        discovery_stub.list_services({.full = true}, error::panic);
    auto const &sum_service_info = full_services.services[1];
    auto const &sum_info = sum_service_info.methods[0];
    if (sum_info.request_type != 1 || sum_info.response_type != 2
        || sum_service_info.types.size() != 4
        || str(sum_service_info.types[0].name) != "example.SumRequest"
        || sum_service_info.types[0].fields.size() != 2
        || str(sum_service_info.types[0].fields[0].name) != "left") {
        t.errorf("full discovery returned incorrect SumService.sum schema");
    }

    auto const &can_service_info = full_services.services[2];
    if (can_service_info.methods[0].response_type != 2
        || can_service_info.types.size() != 2
        || str(can_service_info.types[1].name) != "void") {
        t.errorf("full discovery returned incorrect shared void schema");
    }

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
