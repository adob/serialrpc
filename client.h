#pragma once
#include <initializer_list>
#include <memory>
#include <span>
#include <sys/types.h>
#include <unistd.h>
#include <atomic>
#include <boost/unordered/unordered_flat_map.hpp>

#include "lib/io/io.h"
#include "lib/sync/atomic.h"
#include "lib/sync/cond.h"
#include "lib/sync/go.h"
#include "lib/sync/mutex.h"

#include "encoding.h"
#include "rpc.h"
#include "generated/serialrpc_protocol.pb_msg.h"

namespace serialrpc {
    using namespace lib;

    struct Client;
    

    struct Stub {
        int rpc_offset = 0;
        std::shared_ptr<Client> client;

        str uuid;
        int major_version = 0;
        int minor_version = 0;
        str name;
    } ;

    std::shared_ptr<Client> connect(std::shared_ptr<lib::io::ReaderWriter> const &conn, std::initializer_list<Stub*> service_infos, error err);

    struct Client {
        std::shared_ptr<lib::io::ReaderWriter> conn;
        sync::Mutex event_callbacks_mtx;
        boost::unordered_flat_map<uint32, std::function<void(lib::io::ReaderWriter &, error)>> event_callbacks;

        Client() : conn(nil) {}
        Client(std::shared_ptr<lib::io::ReaderWriter> const &conn);

        void init(std::shared_ptr<lib::io::ReaderWriter> const &conn);
        void close(error err);
        void wait(error err);

        ~Client();

      protected:

        struct Waiter {
            std::atomic<int> state = 0;

            void notify();
            void wait();
        } ;

        struct CallData {
            CallData *next = nil;
            
            Waiter response_received;

            Client *client = nil;
            void *resp = nil;
            error *err = nil;
            str service_name;
            str procedure_name;
            void (*unmarshal)(CallData*, io::Reader &in, error err) = nil;
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

        sync::Mutex call_mtx;  // held during calls
        sync::Mutex data_mtx;  // held when modifying call data list
        sync::Mutex cond_mutex;
        sync::Cond  call_cond;
        sync::go input_worker;
        Error *err = nil;

        CallData *head = nil;
        CallData *tail = nil;

      public:
        template <typename Req, typename Resp>
        Resp call(uint32 rpc_id, str service_name, str procedure_name, Req const &req, error err) {
            Client &c = *this;
            Resp resp;
            CallData call_data = {
                .client    = this,
                .resp      = &resp,
                .err       = &err,
                .service_name = service_name,
                .procedure_name = procedure_name,
                .unmarshal = [](CallData *cd, io::Reader &in, error err) {
                    Resp *resp = (Resp*) cd->resp;
                    *resp = unmarshal<Resp>(in, err);
                },
            };

            {
                sync::Lock lock(c.call_mtx);
                c.start_request(rpc_id, &call_data, err);
                if (err) {
                    fail(lock);
                    return {};
                }
                marshal(*conn, req, err);
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
            
            call_data.response_received.wait();
            return resp;
        }

        template <typename Resp>
        Resp call(uint32 rpc_id, str service_name, str procedure_name, error err) {
            Client &c = *this;
            Resp resp;
            CallData call_data = {
                .client    = this,
                .resp      = &resp,
                .err       = &err,
                .service_name = service_name,
                .procedure_name = procedure_name,
                .unmarshal = [](CallData *cd, io::Reader &in, error err) {
                    Resp *resp = (Resp*) cd->resp;
                    *resp = unmarshal<Resp>(in, err);
                },
            };

            {
                sync::Lock lock(c.call_mtx);
                c.start_request(rpc_id, &call_data, err);
                if (err) {
                    fail(lock);
                    return {};
                }
                conn->write_byte(Tag::End, err);
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
            
            call_data.response_received.wait();
            return resp;
        }

        template <typename Req>
        void call_void(uint32 rpc_id, str service_name, str procedure_name, Req const &req, error err) {
            Client &c = *this;
            
            CallData call_data = {
                .client    = this,
                .err       = &err,
                .service_name = service_name,
                .procedure_name = procedure_name,
            };

            {
                sync::Lock lock(c.call_mtx);
                c.start_request(rpc_id, &call_data, err);
                if (err) {
                    fail(lock);
                    return;
                }
                marshal(*conn, req, err);
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
            
            call_data.response_received.wait();
            return;
        }

        void call_void(uint32 rpc_id, str service_name, str procedure_name, error err) {
            Client &c = *this;
            
            CallData call_data = {
                .client    = this,
                .err       = &err,
                .service_name = service_name,
                .procedure_name = procedure_name,
            };

            {
                sync::Lock lock(c.call_mtx);
                c.start_request(rpc_id, &call_data, err);
                if (err) {
                    fail(lock);
                    return;
                }
                conn->write_byte(Tag::End, err);
                if (err) {
                    fail(lock);
                    return;
                }
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
            
            call_data.response_received.wait();
            return;
        }

        template <typename T>
        void subscribe(uint32 event_id, str service_name, str procedure_name, T const &req, error err) {
            printf("subscribe with event_id %u\n", event_id);
            Client &c = *this;
            CallData call_data = {
                .client    = this,
                .err       = &err,
                .service_name = service_name,
                .procedure_name = procedure_name,
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
            
            call_data.response_received.wait();
        }

        void subscribe(uint32 event_id, str service_name, str procedure_name, error err) {
             Client &c = *this;
            CallData call_data = {
                .client    = this,
                .err       = &err,
                .service_name = service_name,
                .procedure_name = procedure_name,
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
                c.conn->write_byte(Tag::End, err);
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
            
            call_data.response_received.wait();
        }

        void unsubscribe(uint32 event_id, str service_name, str procedure_name, error err) {
            Client &c = *this;
            CallData call_data = {
                .client    = this,
                .err       = &err,
                .service_name = service_name,
                .procedure_name = procedure_name,
            };
            {
                sync::Lock lock(c.call_mtx);
                c.start_request(event_id, &call_data, err);
                if (err) {
                    c.fail(lock);
                    return;
                }

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
            
            call_data.response_received.wait();
        }

        void register_event_callback(
            uint32 event_id,
            std::function<void(lib::io::ReaderWriter &, error)> cb);

        void unregister_event_callback(uint32 event_id);

      protected:
        void start(std::span<Stub *const> stubs, std::shared_ptr<Client> const &shared_client, error err);
        void start_unlocked(std::span<Stub *const> stubs, std::shared_ptr<Client> const &shared_client, error err);
        void check_running(error err);
        void start_request(uint32 rpc_id, CallData *call_data, error err);
        void finish_request(error err);
        void input(std::span<Stub *const> stubs, std::shared_ptr<Client> const &shared_client);
        void client_hello(std::span<Stub *const> service_infos, std::shared_ptr<Client> const &shared_client, error err);
        void read_services_def(std::span<Stub *const> service_infos, std::shared_ptr<Client> const &shared_client, error err);
        void handle_service_def(serialrpcpb::ServiceDef const &service_def, std::span<Stub *const> service_infos, std::shared_ptr<Client> const &shared_client, int offset, error err);
        void fail(sync::Lock const&);
        Client::CallData *pop_call_data();
        void fail_pending_calls(Error &e);
        void handle_reply(error err);
        void handle_chunked_error_reply(error err);
        void handle_log(error err);
        void handle_event(uint32 event_id, error err);

        friend std::shared_ptr<Client> connect(std::shared_ptr<lib::io::ReaderWriter> const &conn, std::initializer_list<Stub*> service_infos, error err);
    } ;
}
