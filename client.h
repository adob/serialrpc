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

        ~ClientBase();

      protected:

        struct Waiter {
            std::atomic<int> state = 0;

            void notify();
            void wait();
        } ;

        struct CallData {
            CallData *next = nil;
            
            Waiter response_received;

            ClientBase *client = nil;
            void *resp = nil;
            error *err = nil;
            void (*unmarshal)(CallData*, io::Reader &in) = nil;
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
            Resp resp;
            CallData call_data = {
                .client    = this,
                .resp      = &resp,
                .err       = &err,
                .unmarshal = [](CallData *cd, io::Reader &in) {
                    Resp *resp = (Resp*) cd->resp;
                    *resp = unmarshal<Resp>(in, *cd->err);
                },
            };

            {
                sync::Lock lock(c.call_mtx);
                c.start_request(rpc_id, &call_data, err);
                if (err) {
                    fail(lock);
                    return {};
                }
                fmt::printf("client: start request done\n");
                marshal(*conn, req, err);
                fmt::printf("client: marshal request done\n");
                if (err) {
                    fail(lock);
                    return {};
                }
                finish_request(err);    
                if (err) {
                    fail(lock);
                    return {};
                }
            }
            
            fmt::printf("sent request; now waiting for data\n");
            call_data.response_received.wait();
            return resp;
        }

        template <typename Req>
        void call_void(uint32 rpc_id, Req const &req, error err) {
            ClientBase &c = *this;
            
            CallData call_data = {
                .client    = this,
                .err       = &err,
            };

            {
                sync::Lock lock(c.call_mtx);
                c.start_request(rpc_id, &call_data, err);
                if (err) {
                    fail(lock);
                    return;
                }
                // fmt::printf("client: start request done\n");
                marshal(*conn, req, err);
                // fmt::printf("client: marshal request done\n");
                if (err) {
                    fail(lock);
                    return;
                }
                finish_request(err);    
                if (err) {
                    fail(lock);
                    return;
                }
            }
            
            // fmt::printf("sent request; now waiting for data\n");
            call_data.response_received.wait();
            return;
        }

        template <typename T>
        void subscribe(uint32 event_id, T const &req, error err) {
            ClientBase &c = *this;
            CallData call_data = {
                .client    = this,
                .err       = &err,
            };
            {
                sync::Lock lock(c.call_mtx);
                c.start_request(event_id, &call_data, err);
                if (err) {
                    c.fail(lock);
                    return;
                }
                c.conn->write_byte(1, err);
                if (err) {
                    c.fail(lock);
                    return;;
                }
                marshal(*c.conn, req, err);
                // fmt::printf("client: marshal subscribe done\n");
                if (err) {
                    c.fail(lock);
                    return;
                }
                finish_request(err);    
                if (err) {
                    c.fail(lock);
                    return;
                }
            }
            
            // fmt::printf("sent request; now waiting for data\n");
            call_data.response_received.wait();
        }

        void unsubscribe(uint32 event_id, error err) {
            ClientBase &c = *this;
            CallData call_data = {
                .client    = this,
                .err       = &err,
            };
            {
                sync::Lock lock(c.call_mtx);
                c.start_request(event_id, &call_data, err);
                if (err) {
                    c.fail(lock);
                    return;
                }
                // fmt::printf("client: start request done\n"); 
                c.conn->write_byte(0, err);
                if (err) {
                    c.fail(lock);
                    return;;
                }
                c.finish_request(err);    
                if (err) {
                    c.fail(lock);
                    return;
                }
            }
            
            // fmt::printf("sent request; now waiting for data\n");
            call_data.response_received.wait();
        }

        void start_unlocked(error err);
        void start_request(uint32 rpc_id, CallData *call_data, error err);
        void finish_request(error err);
        void handle_error_response(error err);
        void input();
        void client_hello(error err);
        void fail(sync::Lock const&);
        void handle_reply(ServerMessageType type, error);
        void handle_log(error err);
    } ;
}
