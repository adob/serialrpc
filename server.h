#pragma once


#include "lib/sync/lock.h"
#include "rpc.h"
#include "encoding.h"

#include "lib/error.h"
#include "lib/io/io.h"
#include "lib/serial/serial_listener.h"
#include "serialrpc/generated/serialrpc_protocol.pb_msg.h"
#include <type_traits>

namespace serialrpc {
    using namespace lib;

    using DispatchFunc = void(*)(void *, serial::Conn &conn, int rpc_id, lib::error err);

    struct ServerErrorHandler : ErrorReporter {
        serial::Conn &conn;
        error err;

        ServerErrorHandler( serial::Conn &conn, error err);

        virtual void handle(Error &) override;
    } ;

    void send_code(serial::Conn &conn, ServerMessageType code, error err);

    struct ServerBase {
        serial::Conn *conn = nil;

        void serve(serial::Listener &listener, error err);
        void accept(serial::Conn &conn, error err);
        void send_goodbye(error err);

      protected:
        virtual void handle_request(uint32 rpc_id, serial::Conn &conn, error err) = 0;
        virtual void unsubscribe_all() = 0;

        void stop_accept();
        void server_hello(serial::Conn &conn, error err);
        void handle_goodbye(error err);

        virtual void send_services_descriptions(error err) = 0;

        friend ServerErrorHandler;
    } ;

    template <typename T>
    serialrpcpb::ServiceDef to_service_def() {
        serialrpcpb::ServiceDef def;
        def.uuid = str(T::UUID);
        def.major_version = T::MajorVersion;
        def.minor_version = T::MinorVersion;
        def.num_endpoints = (int) T::dispatch_table.size();
        
        return def;
    }

    template <typename ...Services>
    struct Server : ServerBase {
        struct DispatchInfo {
            void *service;
            DispatchFunc func;
        } ;

        std::tuple<Services&...> service_refs;
        DispatchInfo dispatch_table[(Services::dispatch_table.size() + ...)];

        Server(Services&... services) : service_refs(services...) {
            size_t idx = 0;
            ((write_dispatch_table(services, idx), idx += Services::dispatch_table.size()), ...);
        }

        template <typename T>
        void write_dispatch_table(T &service, size_t idx) {
            for (size_t i = 0; i < T::dispatch_table.size(); i++) {
                dispatch_table[idx + i] = DispatchInfo{service.service_ptr(), T::dispatch_table[i]};
            }
        }

        template <typename T>
        void send_service_description(T const &service, error err) {
            Stack stack;
            marshal_field(*this->conn, serialrpcpb::ServerHello::ServicesFieldNumber, service, err, MaxNesting, stack);
        }

        void send_services_descriptions(error err) override {
            Server &s = *this;
            // in C++26: template for (auto &&service : service_refs) ...
            std::apply(
                [&](auto&&... service) {
                    ((s.send_service_description(to_service_def<std::remove_cvref_t<decltype(service)>>(), err), !err) && ...);
                },
                std::forward<decltype(service_refs)>(service_refs)
            );
        }

        void handle_request(uint32 rpc_id, serial::Conn &conn, error err) override {
            Server &s = *this;
            printf("server: handle_request: rpc_id=%d\n", rpc_id);
            if (rpc_id > len(s.dispatch_table)) {
                send_code(conn, ServerMessageType::Unknown, err);
                return;
            }

            DispatchInfo &info = s.dispatch_table[rpc_id];
            info.func(info.service, conn, rpc_id, err);
        }
        virtual void unsubscribe_all() override {
            Server &s = *this;
            // in C++26: template for (auto &&service : service_refs) ...
            std::apply(
                [&](auto&&... service) {
                    (service.unsubscribe_all(), ...);
                },
                std::forward<decltype(s.service_refs)>(s.service_refs)
            );
        }
    } ;

    struct CallCtx;

    template <typename T,typename Req, typename Resp>
    using MemberFunc = Resp (T::*)(Req const&, error);

     template <typename T, typename Resp>
    using MemberFuncNoReq = Resp (T::*)(error);

    struct Service {
        virtual void start() {};
        virtual void handle(CallCtx &ctx, int method_id, str data, error err) = 0;

        virtual ~Service() {}

      protected:
        template <typename T, typename Req, typename Resp>
        void handle_method(
            CallCtx &ctx,
            T &t,
            MemberFunc<T, Req, Resp> handler, str msg, error err);

          template <typename T, typename Resp>
          void handle_method(
            CallCtx &ctx,
            T &t,
            MemberFuncNoReq<T, Resp> handler, error err);
    };

    void send_reply_void(serial::Conn &conn, error err);
    void start_reply(serial::Conn &conn, error err);
    void finish_msg(serial::Conn &conn, error err);
    void start_event(serial::Conn &conn,uint32 event_id, error err);
    
    template <typename T>
    void send_reply_msg(serial::Conn &conn, T const &msg, error err) {
        sync::Lock lock(conn.write_mtx);

        start_reply(conn, err);
        if (err) {
            return;
        }

        marshal(conn, msg, err);
        if (err) {
            return;
        }

        finish_msg(conn, err);
        if (err) {
            return;
        }
    }

    template <typename T>
    void send_event(serial::Conn &conn, uint32 event_id, T const &msg) {
        sync::Lock lock(conn.write_mtx);

        ErrorFunc err = [](Error&) {};
        start_event(conn, event_id, err);
        if (err) {
            // s.fail();
            return;
        }

        marshal(conn, msg, err);
        if (err) {
            // s.fail();
            return;
        }

        finish_msg(conn, err);
        if (err) {
            // s.fail();
            return;
        }
    }

    void send_event(serial::Conn &conn, uint32 event_id);
}
