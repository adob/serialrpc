module;

#include "rpc.h"
#include "server_impl.h"

export module serialrpc.server;

import lib;
import lib.sync;
import lib.io;
import lib.serial;
import serialrpc.encoding;
import serialrpc.generated.serialrpc_protocol.msg;
import serialrpc.method_info;
import serialrpc.service_info;

import <array>;
import <tuple>;
import <type_traits>;
import <utility>;

export extern "C++"
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
        void serve(serial::Listener &listener, error err);
        void accept(serial::Conn &conn, error err);
        void send_goodbye(serial::Conn &conn, error err);

      protected:
        virtual void handle_request(uint32 rpc_id, serial::Conn &conn, error err) = 0;
        virtual void unsubscribe_all() = 0;

        void stop_accept();
        void server_hello(serial::Conn &conn, error err);
        void handle_goodbye(serial::Conn &conn, error err);

        virtual void send_services_descriptions(serial::Conn &conn, error err) = 0;

        friend ServerErrorHandler;
    } ;

    inline serialrpcpb::ServiceDef to_service_def(ServiceInfo const &info) {
        serialrpcpb::ServiceDef def;
        def.uuid = str(info.uuid);
        def.major_version = info.major_version;
        def.minor_version = info.minor_version;
        def.num_endpoints = info.num_endpoints;

        return def;
    }

    struct ServiceDescription {
        ServiceInfo info;
        view<MethodInfo> methods;
        view<TypeInfo const*> types;
    };

    template <typename T>
    constexpr ServiceDescription describe_service() {
        static_assert(T::Info.num_endpoints == T::dispatch_table.size());
        return {
            T::Info,
            {T::Methods.data(), T::Methods.size()},
            {T::Types.data(), T::Types.size()},
        };
    }

    template <typename T, size_t... Sizes>
    consteval auto concatenate_arrays(std::array<T, Sizes> const&... arrays) {
        std::array<T, (Sizes + ...)> result{};
        size_t offset = 0;

        auto append = [&]<size_t N>(std::array<T, N> const& array) {
            for (T const& value : array) {
                result[offset++] = value;
            }
        };
        (append(arrays), ...);
        return result;
    }

    struct DiscoveryServiceImpl {
        static constexpr ServiceInfo Info = serialrpcpb::DiscoveryService::Info;
        static constexpr auto Methods = serialrpcpb::DiscoveryService::Methods;
        static constexpr auto Types = serialrpcpb::DiscoveryService::Types;

        view<ServiceDescription> services;

        explicit DiscoveryServiceImpl(view<ServiceDescription> services)
            : services(services) {}

        static void dispatch_list_services(
            void *service, serial::Conn &conn, int rpc_id, error err);

        static constexpr std::array<DispatchFunc, 1> dispatch_table = {
            dispatch_list_services,
        };

        DiscoveryServiceImpl* service_ptr() { return this; }
        void unsubscribe_all() {}
    };

    template <typename ...Services>
    struct Server : ServerBase {
        using ServiceRefs =
            std::tuple<DiscoveryServiceImpl&, Services&...>;

        template <size_t ServiceIndex, size_t MethodIndex>
        static void dispatch_thunk(
            void *server_ptr, serial::Conn &conn, int rpc_id, error err) {
            Server &server = *static_cast<Server*>(server_ptr);
            auto &service = std::get<ServiceIndex>(server.service_refs);
            using ServiceType = std::remove_cvref_t<decltype(service)>;

            ServiceType::dispatch_table[MethodIndex](
                service.service_ptr(), conn, rpc_id, err);
        }

        template <size_t ServiceIndex, size_t... MethodIndices>
        static consteval auto make_service_dispatch_table(
            std::index_sequence<MethodIndices...>) {
            return std::array<DispatchFunc, sizeof...(MethodIndices)>{
                dispatch_thunk<ServiceIndex, MethodIndices>...,
            };
        }

        template <size_t ServiceIndex>
        static consteval auto make_service_dispatch_table() {
            using ServiceType = std::remove_reference_t<
                std::tuple_element_t<ServiceIndex, ServiceRefs>>;
            return make_service_dispatch_table<ServiceIndex>(
                std::make_index_sequence<
                    ServiceType::dispatch_table.size()>{});
        }

        template <size_t... ServiceIndices>
        static consteval auto make_dispatch_table(
            std::index_sequence<ServiceIndices...>) {
            return concatenate_arrays(
                make_service_dispatch_table<ServiceIndices>()...);
        }

        inline static constexpr auto dispatch_table = make_dispatch_table(
            std::make_index_sequence<1 + sizeof...(Services)>{});

        std::array<ServiceDescription, 1 + sizeof...(Services)>
            service_descriptions;
        DiscoveryServiceImpl discovery_service;
        ServiceRefs service_refs;

        Server(Services&... services)
            : service_descriptions{
                describe_service<DiscoveryServiceImpl>(),
                describe_service<Services>()...,
              },
              discovery_service({
                  service_descriptions.data(), service_descriptions.size()}),
              service_refs(discovery_service, services...) {}

        void send_service_description(serial::Conn &conn,
                                      serialrpcpb::ServiceDef const &service,
                                      error err);

        void send_services_descriptions(serial::Conn &conn, error err) override;

        void handle_request(uint32 rpc_id, serial::Conn &conn, error err) override {
            Server &s = *this;
            if (rpc_id >= uint32(len(s.dispatch_table))) {
                send_code(conn, ServerMessageType::Unknown, err);
                return;
            }

            s.dispatch_table[rpc_id](&s, conn, rpc_id, err);
        }
        virtual void unsubscribe_all() override {
            Server &s = *this;
            std::apply(
                [&](auto&&... service) {
                    (service.unsubscribe_all(), ...);
                },
                std::forward<decltype(s.service_refs)>(s.service_refs)
            );
        }
    };
    template <typename... Services>
    inline void Server<Services...>::send_service_description(
        serial::Conn &conn, serialrpcpb::ServiceDef const &service, error err) {
      Stack stack;
      marshal_field(conn, serialrpcpb::ServerHello::ServicesFieldNumber,
                    service, err, MaxNesting, stack);
    }
    template <typename... Services>
    inline void
    Server<Services...>::send_services_descriptions(serial::Conn &conn,
                                                    error err) {
      Server &s = *this;

      for (ServiceDescription const &desc : s.service_descriptions) {
        s.send_service_description(conn, to_service_def(desc.info), err);
        if (err) {
          return;
        }
      }
    }

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
