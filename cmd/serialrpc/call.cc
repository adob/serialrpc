#include "call.h"

#include <memory>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>

import lib.fmt;
#include "serialrpc/client.h"
#include "serialrpc/encoding.h"
#include "serialrpc/generated/serialrpc_protocol.pb_client.h"

using namespace lib;

namespace serialrpc::cli {
namespace {

using google::protobuf::Descriptor;
using google::protobuf::DescriptorPool;
using google::protobuf::DynamicMessageFactory;
using google::protobuf::FieldDescriptor;
using google::protobuf::DescriptorProto;
using google::protobuf::EnumDescriptorProto;
using google::protobuf::FieldDescriptorProto;
using google::protobuf::FileDescriptorProto;
using google::protobuf::Message;
using google::protobuf::MethodDescriptor;
using google::protobuf::Reflection;
using google::protobuf::ServiceDescriptor;
using google::protobuf::TextFormat;

std::string to_string(str s) {
    return {s.data, size_t(len(s))};
}

void marshal_message(Message const &message, io::Writer &out, error err,
                     int nesting, Stack &stack);

void marshal_field_value(Message const &message, FieldDescriptor const &field,
                         int index, io::Writer &out, error err, int nesting,
                         Stack &stack) {
    Reflection const &reflection = *message.GetReflection();
    bool repeated = field.is_repeated();
    int number = field.number();

    switch (field.cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
        marshal_field(out, number,
            repeated ? reflection.GetRepeatedInt32(message, &field, index)
                     : reflection.GetInt32(message, &field),
            err, nesting, stack);
        break;
    case FieldDescriptor::CPPTYPE_INT64:
        marshal_field(out, number,
            repeated ? reflection.GetRepeatedInt64(message, &field, index)
                     : reflection.GetInt64(message, &field),
            err, nesting, stack);
        break;
    case FieldDescriptor::CPPTYPE_UINT32:
        marshal_field(out, number,
            repeated ? reflection.GetRepeatedUInt32(message, &field, index)
                     : reflection.GetUInt32(message, &field),
            err, nesting, stack);
        break;
    case FieldDescriptor::CPPTYPE_UINT64:
        marshal_field(out, number,
            repeated ? reflection.GetRepeatedUInt64(message, &field, index)
                     : reflection.GetUInt64(message, &field),
            err, nesting, stack);
        break;
    case FieldDescriptor::CPPTYPE_DOUBLE:
        marshal_field(out, number,
            repeated ? reflection.GetRepeatedDouble(message, &field, index)
                     : reflection.GetDouble(message, &field),
            err, nesting, stack);
        break;
    case FieldDescriptor::CPPTYPE_FLOAT:
        marshal_field(out, number,
            repeated ? reflection.GetRepeatedFloat(message, &field, index)
                     : reflection.GetFloat(message, &field),
            err, nesting, stack);
        break;
    case FieldDescriptor::CPPTYPE_BOOL:
        marshal_field(out, number,
            repeated ? reflection.GetRepeatedBool(message, &field, index)
                     : reflection.GetBool(message, &field),
            err, nesting, stack);
        break;
    case FieldDescriptor::CPPTYPE_ENUM:
        marshal_field(out, number, int32(
            repeated ? reflection.GetRepeatedEnumValue(message, &field, index)
                     : reflection.GetEnumValue(message, &field)),
            err, nesting, stack);
        break;
    case FieldDescriptor::CPPTYPE_STRING: {
        std::string value = repeated
            ? reflection.GetRepeatedString(message, &field, index)
            : reflection.GetString(message, &field);
        marshal_field(out, number, str(value), err, nesting, stack);
        break;
    }
    case FieldDescriptor::CPPTYPE_MESSAGE: {
        Message const &value = repeated
            ? reflection.GetRepeatedMessage(message, &field, index)
            : reflection.GetMessage(message, &field);
        stack.push(uint32(number));
        marshal_message(value, out, err, nesting - 1, stack);
        if (err) {
            return;
        }
        if (stack.size == 0) {
            out.write_byte(Tag::End, err);
        } else {
            stack.pop();
        }
        break;
    }
    }
}

void marshal_message(Message const &message, io::Writer &out, error err,
                     int nesting, Stack &stack) {
    if (nesting < 0) {
        err("serialrpc: excessive message nesting");
        return;
    }

    Reflection const &reflection = *message.GetReflection();
    std::vector<FieldDescriptor const*> fields;
    reflection.ListFields(message, &fields);
    for (FieldDescriptor const *field : fields) {
        int count = field->is_repeated()
            ? reflection.FieldSize(message, field) : 1;
        for (int i = 0; i < count; ++i) {
            marshal_field_value(message, *field, i, out, err, nesting, stack);
            if (err) {
                return;
            }
        }
    }
}

struct DynamicRequest : io::WriterTo {
    Message const &message;

    explicit DynamicRequest(Message const &message) : message(message) {}

    void write_to(io::Writer &out, error err) const override {
        detail::marshal(message, out, err);
    }
};

void unmarshal_message(Message &message, io::Reader &in, error err,
                       int nesting) {
    if (nesting < 0) {
        err("serialrpc: excessive message nesting");
        return;
    }

    Descriptor const &descriptor = *message.GetDescriptor();
    Reflection const &reflection = *message.GetReflection();
    for (;;) {
        Tag tag = read_tag(in, err);
        if (err || tag.type == Tag::End) {
            return;
        }

        FieldDescriptor const *field =
            descriptor.FindFieldByNumber(tag.field_num);
        if (field == nil) {
            skip(in, tag.type, err, nesting);
            if (err) {
                return;
            }
            continue;
        }

        bool repeated = field->is_repeated();
        switch (field->cpp_type()) {
        case FieldDescriptor::CPPTYPE_INT32: {
            int32 value = unmarshal<int32>(in, err, nesting - 1);
            if (repeated) reflection.AddInt32(&message, field, value);
            else reflection.SetInt32(&message, field, value);
            break;
        }
        case FieldDescriptor::CPPTYPE_INT64: {
            int64 value = unmarshal<int64>(in, err, nesting - 1);
            if (repeated) reflection.AddInt64(&message, field, value);
            else reflection.SetInt64(&message, field, value);
            break;
        }
        case FieldDescriptor::CPPTYPE_UINT32: {
            uint32 value = unmarshal<uint32>(in, err, nesting - 1);
            if (repeated) reflection.AddUInt32(&message, field, value);
            else reflection.SetUInt32(&message, field, value);
            break;
        }
        case FieldDescriptor::CPPTYPE_UINT64: {
            uint64 value = unmarshal<uint64>(in, err, nesting - 1);
            if (repeated) reflection.AddUInt64(&message, field, value);
            else reflection.SetUInt64(&message, field, value);
            break;
        }
        case FieldDescriptor::CPPTYPE_DOUBLE: {
            double value = unmarshal<double>(in, err, nesting - 1);
            if (repeated) reflection.AddDouble(&message, field, value);
            else reflection.SetDouble(&message, field, value);
            break;
        }
        case FieldDescriptor::CPPTYPE_FLOAT: {
            float value = unmarshal<float>(in, err, nesting - 1);
            if (repeated) reflection.AddFloat(&message, field, value);
            else reflection.SetFloat(&message, field, value);
            break;
        }
        case FieldDescriptor::CPPTYPE_BOOL: {
            bool value = unmarshal<bool>(in, err, nesting - 1);
            if (repeated) reflection.AddBool(&message, field, value);
            else reflection.SetBool(&message, field, value);
            break;
        }
        case FieldDescriptor::CPPTYPE_ENUM: {
            int value = unmarshal<int32>(in, err, nesting - 1);
            if (repeated) reflection.AddEnumValue(&message, field, value);
            else reflection.SetEnumValue(&message, field, value);
            break;
        }
        case FieldDescriptor::CPPTYPE_STRING: {
            str value = unmarshal<str>(in, err, nesting - 1);
            std::string text(value.data, size_t(len(value)));
            if (repeated) reflection.AddString(&message, field, text);
            else reflection.SetString(&message, field, text);
            break;
        }
        case FieldDescriptor::CPPTYPE_MESSAGE: {
            Message *value = repeated
                ? reflection.AddMessage(&message, field)
                : reflection.MutableMessage(&message, field);
            unmarshal_message(*value, in, err, nesting - 1);
            break;
        }
        }
        if (err) {
            return;
        }
    }
}

void unmarshal_response(void *response, io::Reader &in, error err) {
    detail::unmarshal(*static_cast<Message*>(response), in, err);
}

bool split_method(std::string const &name, std::string &service,
                  std::string &method) {
    size_t separator = name.rfind('/');
    if (separator == std::string::npos) {
        separator = name.rfind('.');
    }
    if (separator == std::string::npos || separator == 0
        || separator + 1 == name.size()) {
        return false;
    }
    service = name.substr(0, separator);
    method = name.substr(separator + 1);
    return true;
}

MethodDescriptor const *build_method_descriptor_impl(
    serialrpcpb::ServiceInfo const &service,
    serialrpcpb::MethodInfo const &method, DescriptorPool &pool, error err) {
    if (method.request_type == 0 || method.response_type == 0
        || service.types.empty()) {
        err("serialrpc: server does not support full discovery");
        return nil;
    }

    FileDescriptorProto file;
    file.set_name("serialrpc_dynamic.proto");
    file.set_package("serialrpc_dynamic");
    file.set_syntax("proto3");

    std::map<uint32, std::string> message_names;
    std::map<uint32, int> message_indexes;
    std::map<uint32, std::string> enum_names;
    std::map<uint32, int> enum_container_indexes;
    std::set<uint32> type_ids;
    for (auto const &type : service.types) {
        if (type.id == 0) {
            err("serialrpc: discovered type has ID zero");
            return nil;
        }
        if (!type_ids.insert(type.id).second) {
            err("serialrpc: duplicate discovered type ID %v", type.id);
            return nil;
        }
        if (type.type == serialrpcpb::FieldType::FieldTypeMessage) {
            std::string generated = "Message" + std::to_string(type.id);
            message_names.emplace(type.id,
                                  ".serialrpc_dynamic." + generated);
            message_indexes.emplace(type.id, file.message_type_size());
            file.add_message_type()->set_name(generated);
        } else if (type.type == serialrpcpb::FieldType::FieldTypeEnum) {
            std::string generated = "Enum" + std::to_string(type.id);
            enum_names.emplace(type.id,
                               ".serialrpc_dynamic." + generated + ".Value");
            enum_container_indexes.emplace(type.id, file.message_type_size());
            DescriptorProto *container = file.add_message_type();
            container->set_name(generated);
            container->add_enum_type()->set_name("Value");
        } else {
            err("serialrpc: discovered type %v is not a message or enum",
                type.id);
            return nil;
        }
    }

    for (auto const &source : service.types) {
        if (source.type == serialrpcpb::FieldType::FieldTypeEnum) {
            int container_index = enum_container_indexes.at(source.id);
            EnumDescriptorProto *destination =
                file.mutable_message_type(container_index)->mutable_enum_type(0);
            std::set<int32> numbers;
            bool has_alias = false;
            for (auto const &source_value : source.enum_values) {
                auto *value = destination->add_value();
                value->set_name(to_string(str(source_value.name)));
                value->set_number(source_value.number);
                if (!numbers.insert(source_value.number).second) {
                    has_alias = true;
                }
            }
            if (has_alias) {
                destination->mutable_options()->set_allow_alias(true);
            }
            continue;
        }

        int message_index = message_indexes.at(source.id);
        DescriptorProto *destination =
            file.mutable_message_type(message_index);
        for (auto const &source_field : source.fields) {
            int field_type = int(source_field.type);
            if (field_type < FieldDescriptorProto::TYPE_DOUBLE
                || field_type > FieldDescriptorProto::TYPE_SINT64
                || field_type == FieldDescriptorProto::TYPE_GROUP) {
                err("serialrpc: field %q has invalid type %v",
                    str(source_field.name), field_type);
                return nil;
            }
            FieldDescriptorProto *field = destination->add_field();
            field->set_name(to_string(str(source_field.name)));
            field->set_number(int32_t(source_field.number));
            field->set_label(source_field.repeated
                ? FieldDescriptorProto::LABEL_REPEATED
                : FieldDescriptorProto::LABEL_OPTIONAL);
            field->set_type(FieldDescriptorProto::Type(field_type));

            if (source_field.type
                == serialrpcpb::FieldType::FieldTypeMessage) {
                auto found = message_names.find(source_field.type_id);
                if (found == message_names.end()) {
                    err("serialrpc: unknown discovered message type %v",
                        source_field.type_id);
                    return nil;
                }
                field->set_type_name(found->second);
            } else if (source_field.type
                       == serialrpcpb::FieldType::FieldTypeEnum) {
                auto found = enum_names.find(source_field.type_id);
                if (found == enum_names.end()) {
                    err("serialrpc: unknown discovered enum type %v",
                        source_field.type_id);
                    return nil;
                }
                field->set_type_name(found->second);
            }
        }
    }

    auto request = message_names.find(method.request_type);
    auto response = message_names.find(method.response_type);
    if (request == message_names.end() || response == message_names.end()) {
        err("serialrpc: discovered method references an unknown message type");
        return nil;
    }

    auto *dynamic_service = file.add_service();
    dynamic_service->set_name("Service");
    auto *method_descriptor = dynamic_service->add_method();
    method_descriptor->set_name("Method");
    method_descriptor->set_input_type(request->second);
    method_descriptor->set_output_type(response->second);
    method_descriptor->set_client_streaming(method.client_streaming);
    method_descriptor->set_server_streaming(method.server_streaming);

    auto const *built = pool.BuildFile(file);
    if (built == nil) {
        err("serialrpc: could not load discovered method schema");
        return nil;
    }
    return built->service(0)->method(0);
}

} // namespace

void detail::marshal(Message const &message, io::Writer &out, error err) {
    Stack stack;
    marshal_message(message, out, err, MaxNesting, stack);
    if (!err) {
        out.write_byte(Tag::End, err);
    }
}

void detail::unmarshal(Message &message, io::Reader &in, error err) {
    unmarshal_message(message, in, err, MaxNesting);
}

MethodDescriptor const *detail::build_method_descriptor(
    serialrpcpb::ServiceInfo const &service,
    serialrpcpb::MethodInfo const &method, DescriptorPool &pool, error err) {
    return build_method_descriptor_impl(service, method, pool, err);
}

void call(str endpoint, str method_name, str request_text, error err) {
    std::string qualified_method = to_string(method_name);
    std::string service_name;
    std::string short_method_name;
    if (!split_method(qualified_method, service_name, short_method_name)) {
        err("serialrpc: method must be named as <package>.<service>.<method>");
        return;
    }

    serialrpcpb::DiscoveryServiceStub discovery_stub;
    auto client = serialrpc::connect(endpoint, {&discovery_stub}, err);
    if (err) {
        return;
    }

    auto discovery = discovery_stub.list_services({.full = true}, err);
    if (err) {
        return;
    }

    uint32 endpoint_id = 1;
    serialrpcpb::ServiceInfo const *service_info = nil;
    serialrpcpb::MethodInfo const *method_info = nil;
    for (auto const &service : discovery.services) {
        bool service_matches = str(service.name) == str(service_name);
        if (!service_matches) {
            endpoint_id += uint32(service.methods.size());
            continue;
        }

        serialrpcpb::MethodInfo const *target = nil;
        for (auto const &method : service.methods) {
            if (str(method.name) == str(short_method_name)) {
                target = &method;
                break;
            }
        }
        if (target != nil) {
            // Generated dispatch tables order methods by their stable method
            // ID, independently of declaration order in the .proto file.
            for (auto const &method : service.methods) {
                if (method.id < target->id) {
                    ++endpoint_id;
                }
            }
            service_info = &service;
            method_info = target;
            break;
        }
        break;
    }
    if (method_info == nil) {
        err("serialrpc: server does not expose method %q", method_name);
        return;
    }

    DescriptorPool pool(DescriptorPool::generated_pool());
    MethodDescriptor const *method =
        detail::build_method_descriptor(
            *service_info, *method_info, pool, err);
    if (err) {
        return;
    }
    if (method->client_streaming() || method->server_streaming()) {
        err("serialrpc: call does not yet support streaming method %q",
            method_name);
        return;
    }

    DynamicMessageFactory factory(&pool);
    Message const *request_prototype = factory.GetPrototype(method->input_type());
    Message const *response_prototype = factory.GetPrototype(method->output_type());
    if (request_prototype == nil || response_prototype == nil) {
        err("serialrpc: could not create dynamic messages for %q", method_name);
        return;
    }

    auto request = std::unique_ptr<Message>(request_prototype->New());
    auto response = std::unique_ptr<Message>(response_prototype->New());
    auto type_name = [&](uint32 id) -> str {
        for (auto const &type : service_info->types) {
            if (type.id == id) {
                return type.name;
            }
        }
        return {};
    };
    bool request_is_void = type_name(method_info->request_type) == "void";
    bool response_is_void = type_name(method_info->response_type) == "void";

    if (request_is_void) {
        if (len(request_text) != 0) {
            err("serialrpc: method %q has no request message", method_name);
            return;
        }
    } else if (!TextFormat::ParseFromString(to_string(request_text),
                                             request.get())) {
        err("serialrpc: could not parse request for %q", method_name);
        return;
    }

    DynamicRequest request_writer(*request);
    client->call_raw(
        endpoint_id, str(service_name), str(short_method_name),
        request_is_void ? nil : &request_writer,
        response_is_void ? nil : response.get(),
        response_is_void ? nil : unmarshal_response,
        err);
    if (err) {
        return;
    }

    if (response_is_void) {
        fmt::printf("{}\n");
    } else {
        std::string text;
        if (!TextFormat::PrintToString(*response, &text)) {
            err("serialrpc: could not format response from %q", method_name);
            return;
        }
        if (text.empty()) {
            fmt::printf("{}\n");
        } else {
            fmt::printf("%s", str(text));
        }
    }

    client->close(err);
}

} // namespace serialrpc::cli
