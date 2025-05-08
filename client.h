#pragma once
#include <memory>
#include <unistd.h>
#include <cstddef>
#include <type_traits>
#include <atomic>

#include "lib/io/io.h"
#include "lib/sync/atomic.h"
#include "lib/sync/cond.h"
#include "lib/sync/go.h"
#include "lib/sync/mutex.h"

#include "encoding.h"
#include "rpc.h"

namespace serialrpc {
    using namespace lib;

    struct ClientBase {
        std::shared_ptr<lib::io::ReaderWriter> conn;

        ClientBase(std::shared_ptr<lib::io::ReaderWriter> conn);

        void start(error err);
        void close(error err);

    protected:
        struct CallData {
            CallData *next = nil;
            std::atomic<ServerMessageType> type = {};
            std::atomic<bool> response_received = false;
            std::atomic<bool> caller_done = false;

            ~CallData() {
                caller_done.store(true);
                caller_done.notify_one();
            }
        } ;

        enum State {
            New,
            Starting,
            Running,
            Closed,
            Failing,
            Failed
        };
        
        sync::atomic<State> state = New;

        sync::Mutex call_mtx;
        sync::Mutex cond_mutex;
        sync::Cond  call_cond;
        sync::go input_worker;
        Error *err = nil;

        CallData *head = nil;
        CallData *tail = nil;

        virtual void handle_event(uint32 event_id, error err) = 0;

        template <typename Req, typename Resp>
        Resp call(uint32 rpc_id, Req const &req, error err) {
            ClientBase &c = *this;
            CallData call_data;
            {
                sync::Lock lock(call_mtx);
                start_request(rpc_id, &call_data, err);
                if (err) {
                    fail();
                    return {};
                }
                fmt::printf("client: start request done\n");
                marshal_fields(*conn, req, err);
                fmt::printf("client: marshal request done\n");
                if (err) {
                    fail();
                    return {};
                }
                finish_request(err);    
                if (err) {
                    fail();
                    return {};
                }
            }
            
            fmt::printf("sent request; now waiting for data\n");
            call_data.response_received.wait(false);

            ServerMessageType type = call_data.type.load();
            if (type != ServerMessageType::Reply) {
                handle_error_response(err);
                return {};
            }
            
            Resp resp = unmarshal<Resp>(*c.conn, err);
            if (err) {
                fail();
                return {};
            }
            return resp;
        }

        template <typename T>
        void subscribe(uint32 event_id, T const &req, error err) {
            ClientBase &c = *this;
            CallData call_data;
            {
                sync::Lock lock(c.call_mtx);
                c.start_request(event_id, &call_data, err);
                if (err) {
                    c.fail();
                    return;
                }
                c.conn->write_byte(1, err);
                if (err) {
                    c.fail();
                    return;;
                }
                marshal_fields(*c.conn, req, err);
                fmt::printf("client: marshal subscribe done\n");
                if (err) {
                    c.fail();
                    return;
                }
                finish_request(err);    
                if (err) {
                    c.fail();
                    return;
                }
            }
            
            fmt::printf("sent request; now waiting for data\n");
            call_data.response_received.wait(false);

            ServerMessageType type = call_data.type.load();
            if (type != ServerMessageType::Reply) {
                c.handle_error_response(err);
                return;
            }
        }

        void unsubscribe(uint32 event_id, error err) {
            ClientBase &c = *this;
            CallData call_data;
            {
                sync::Lock lock(c.call_mtx);
                c.start_request(event_id, &call_data, err);
                if (err) {
                    c.fail();
                    return;
                }
                fmt::printf("client: start request done\n"); 
                c.conn->write_byte(0, err);
                if (err) {
                    c.fail();
                    return;;
                }
                c.finish_request(err);    
                if (err) {
                    c.fail();
                    return;
                }
            }
            
            fmt::printf("sent request; now waiting for data\n");
            call_data.response_received.wait(false);

            ServerMessageType type = call_data.type.load();
            if (type != ServerMessageType::Reply) {
                handle_error_response(err);
                return;
            }
        }

        void start_unlocked(error err);
        void start_request(uint32 rpc_id, CallData *call_data, error err);
        void finish_request(error err);
        void handle_error_response(error err);
        void input();
        void client_hello(error err);
        void fail();
        void handle_reply(ServerMessageType type);
    } ;

    struct Call {
        uint method_id;
    } ;

    struct Client {
        std::shared_ptr<io::ReaderWriter> iostream;

        Buffer writebuf = Buffer(1024);
        Buffer readbuf = Buffer(1024);
        sync::go input_worker;

        Client(std::shared_ptr<io::ReaderWriter> const& stream);

        str call(uint method_id, str data, error err);
        void start(error err);
        void close(error err);

        template <typename T1, typename T2>
        void call(uint method_id, const T1 &data, T2 &result, error err) {
            if constexpr (!std::is_same_v<T1, const std::nullptr_t>) {
                str binary = serialize(writebuf, data, err);
                if (err) {
                    return;
                }
                str retdata = call(method_id, binary, err);
                if (err) {
                    return;
                }

                if constexpr (!std::is_same_v<T2, const std::nullptr_t>) {
                    deserialize(retdata, result, err);
                }
            } else {
                str retdata = call(method_id, {}, err);
                if (err) {
                    return;
                }

                if constexpr (!std::is_same_v<T2, const std::nullptr_t>) {
                    deserialize(retdata, result, err);
                }
            }
        }

        ~Client();

    private:
        enum State {
            Starting,
            Running,
            Closed,
            Failing,
            Failed
        } state = Starting;

        sync::Mutex call_mutex;
        sync::Mutex cond_mutex;
        sync::Cond  call_cond;

        bool has_reply = false;

        str pending_reply;
        ServerMessageType pending_message_type;
        //deferror *pending_error; 
        //Error pending_error;

        void input();
        str read_body(error);
        void handle_reply(ServerMessageType type, error err);
        void handle_error(ServerMessageType type);

        void client_hello(error);

        Error *err = nil;
    };

}
