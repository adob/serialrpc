#pragma once

#include "lib/error.h"
#include "lib/io/io.h"
#include "lib/str.h"

namespace google::protobuf {
    class DescriptorPool;
    class Message;
    class MethodDescriptor;
}

namespace serialrpcpb {
    struct MethodInfo;
    struct ServiceInfo;
}

namespace serialrpc::cli {
    void call(lib::str endpoint, lib::str method, lib::str request,
              lib::error err);

    namespace detail {
        void marshal(google::protobuf::Message const &message,
                     lib::io::Writer &out, lib::error err);
        void unmarshal(google::protobuf::Message &message,
                       lib::io::Reader &in, lib::error err);
        google::protobuf::MethodDescriptor const *build_method_descriptor(
            serialrpcpb::ServiceInfo const &service,
            serialrpcpb::MethodInfo const &method,
            google::protobuf::DescriptorPool &pool, lib::error err);
    }
}
