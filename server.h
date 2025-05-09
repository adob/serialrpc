#pragma once

#include <atomic>

#include "lib/sync/lock.h"
#include "lib/sync/mutex.h"
#include "rpc.h"
#include "encoding.h"

#include "lib/error.h"
#include "lib/array.h"
#include "lib/io/io.h"
#include "serial/serial_listener.h"

namespace serialrpc {
    using namespace lib;

    struct ServerBase {
        serial::Conn *conn = nil;
        // sync::Mutex send_mtx;

        struct ServerErrorHandler : ErrorReporterInterface {
            io::ReaderWriter &conn;
            error err;

            ServerErrorHandler(io::ReaderWriter &conn, error err);

            virtual void report(Error &) override;
        } ;

        void serve(serial::Listener &listener, error err);
        void accept(serial::Conn &conn, error err);

      protected:
        virtual void handle_request(uint32 rpc_id, io::ReaderWriter &conn, error err) = 0;
        virtual void unsubscribe_all() = 0;

        void serve_request(io::ReaderWriter &conn, error err);
        void stop_accept();
        void fail();

        void start_reply(io::ReaderWriter &conn, error err);
        void start_event(uint32 event_id, error err);

        void finish_msg(io::ReaderWriter &conn, error err);

        void send_code(io::ReaderWriter &conn, ServerMessageType code,
                       error err);

        void send_error(io::ReaderWriter &conn, error const &rpc_error, error err);

        void handle_goodbye(error err);

        template <typename T>
        void send_reply(io::ReaderWriter &conn, T const &msg, error err) {
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

        template <typename T>
        void send_event(uint32 event_id, T const &msg) {
            ServerBase &s = *this;
            sync::Lock lock(s.conn->write_mtx);
            if (s.conn == nil) {
                return;
            }
            ErrorReporter err = [](Error&) {};
            s.start_event(event_id, err);
            if (err) {
                s.fail();
                return;
            }

            marshal(*s.conn, msg, err);
            if (err) {
                s.fail();
                return;
            }

            finish_msg(*s.conn, err);
            if (err) {
                s.fail();
                return;
            }
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

    struct Server {
        Service &service;
        Array<byte, 1024> writebuf;
        Array<byte, 1024> readbuf;

        Server(Service &service);

        void serve(io::ReaderWriter &conn, error err);
        bool handle_rpc(io::ReaderWriter&, error);

    private:
        void handle_hello(io::ReaderWriter&, error);
        void handle_request(io::ReaderWriter&, error);
        void handle_goodbye(io::ReaderWriter&, error);
    };

    struct CallCtx {
        Server &server;
        io::ReaderWriter &conn;

        bool handled = false;
        error err;

        void reply(str data);
        void send_code(ServerMessageType code);
        void send_error(str msg);

        template <typename T>
        void reply(T &msg) {
            str data = serialize(server.writebuf, msg, error::panic);
            reply(data);
        }
    };

    template <typename T, typename Req, typename Resp>
    void Service::handle_method(
            CallCtx &ctx,
            T &t,
            MemberFunc<T, Req, Resp> handler,
            str msg,
            error err) {

        Req req;
        deserialize(msg, req, err);
        if (err) {
            ctx.send_code(serialrpc::BadMessage);
            return;
        };

        if constexpr (!std::is_void_v<Resp>) {
            Resp resp = (t.*handler)(req, err);
            if (err) {
                return;
            }
            ctx.reply(resp);
        } else {
            (t.*handler)(req, err);
            ctx.reply({});
        }
    }

    template <typename T, typename Resp>
    void Service::handle_method(
            CallCtx &ctx,
            T &t,
            MemberFuncNoReq<T, Resp> handler,
            error err) {


        if constexpr (!std::is_void_v<Resp>) {
            Resp resp = (t.*handler)(err);
            if (err) {
                return;
            }
            ctx.reply(resp);
        } else {
            (t.*handler)(err);
            ctx.reply({});
        }
    }
}
