#pragma once


#include "lib/sync/lock.h"
#include "rpc.h"
#include "encoding.h"

#include "lib/error.h"
#include "lib/io/io.h"
#include "serial/serial_listener.h"

namespace serialrpc {
    using namespace lib;

    struct ServerBase {
        serial::Conn *conn = nil;
        // sync::Mutex send_mtx;

        struct ServerErrorHandler : ErrorReporter {
            ServerBase &base;
            error err;

            ServerErrorHandler(ServerBase &base, error err);

            virtual void handle(Error &) override;
        } ;

        void serve(serial::Listener &listener, error err);
        void accept(serial::Conn &conn, error err);
        void send_goodbye(error err);

      protected:
        virtual void handle_request(uint32 rpc_id, io::ReaderWriter &conn, error err) = 0;
        virtual void unsubscribe_all() = 0;

        // void serve_request(io::ReaderWriter &conn, error err);
        void stop_accept();
        // void fail();

        void start_reply(io::ReaderWriter &conn, error err);
        void start_event(uint32 event_id, error err);

        void finish_msg(io::ReaderWriter &conn, error err);

        void send_code(io::ReaderWriter &conn, ServerMessageType code,
                       error err);

        void handle_goodbye(error err);

        template <typename T>
        void send_reply_msg(io::ReaderWriter &conn, T const &msg, error err) {
            ServerBase &s = *this;
            sync::Lock lock(s.conn->write_mtx);

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

        void send_reply_void(io::ReaderWriter &conn, error err);

        template <typename T>
        void send_event(uint32 event_id, T const &msg) {
            ServerBase &s = *this;
            sync::Lock lock(s.conn->write_mtx);
            if (s.conn == nil) {
                return;
            }
            ErrorFunc err = [](Error&) {};
            s.start_event(event_id, err);
            if (err) {
                // s.fail();
                return;
            }

            marshal(*s.conn, msg, err);
            if (err) {
                // s.fail();
                return;
            }

            finish_msg(*s.conn, err);
            if (err) {
                // s.fail();
                return;
            }
        }

        friend ServerErrorHandler;
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
}
