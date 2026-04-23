#include "ProtoCEchoPlugin.h"
#include "lib/print.h"
#include "serialrpc.pb.h"
#include "EchoObjects.h"
#include "google/protobuf/compiler/cpp/helpers.h"
#include "google/protobuf/compiler/plugin.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
// #include "google/protobuf/stubs/strutil.h"
// #include "infra/syntax/ProtoFormatter.hpp"
#include "lib/strconv/itoa.h"
#include <cctype>
#include <google/protobuf/descriptor.h>
#include "CppFormatter.h"
#include <memory>
#include <sstream>

using namespace lib;

static std::string SimpleItoa(size i) {
    String s = strconv::itoa(i);
    return s.std_string();
}

static std::string FilenameIdentifier(const std::string& filename) {
    std::string result;
    for (size_t i = 0; i < filename.size(); ++i) {
        if (absl::ascii_isalnum(filename[i])) {
        result.push_back(filename[i]);
        } else {
        // Not alphanumeric.  To avoid any possibility of name conflicts we
        // use the hex code for the character.
        absl::StrAppend(&result, "_",
                        absl::Hex(static_cast<uint8_t>(filename[i])));
        }
    }
    return result;
}

static std::string CamelCaseToUnderscores(absl::string_view input) {
    std::string out;

    for (size i = 0; i < input.size(); i++) {
        char c =  input[i];
        if (i == 0 || i == input.size() - 1) {
            out += char(tolower(c));
            continue;
        }

        // ABCService -> abc_service
        // AbcService -> abc_service

        if (isupper(c) && !isupper(input[i+1])) {
            out += "_";
        }

        out += char(tolower(c));
    }

    return out;
}

int32_t application::MaxVarIntSize(uint64_t value) {
    uint32_t result = 1;

    while (value > 127)
    {
        value >>= 7;
        ++result;
    }

    return result;
}

namespace application
{
    namespace
    {
        class StorageTypeVisitor
            : public EchoFieldVisitor
        {
        public:
            explicit StorageTypeVisitor(std::string& result)
                : result(result)
            {}

            void VisitInt64(const EchoFieldInt64& field) override
            {
                result = "int64_t";
            }

            void VisitUint64(const EchoFieldUint64& field) override
            {
                result = "uint64_t";
            }

            void VisitInt32(const EchoFieldInt32& field) override
            {
                result = "int32_t";
            }

            void VisitFixed64(const EchoFieldFixed64& field) override
            {
                result = "uint64_t";
            }

            void VisitFixed32(const EchoFieldFixed32& field) override
            {
                result = "uint32_t";
            }

            void VisitBool(const EchoFieldBool& field) override
            {
                result = "bool";
            }

            void VisitString(const EchoFieldString& field) override
            {
                // result = "infra::BoundedString::WithStorage<" + SimpleItoa(field.maxStringSize) + ">";
                result = result = "lib::InlineString<" + SimpleItoa(field.maxStringSize) + ">";
            }

            void VisitUnboundedString(const EchoFieldUnboundedString& field) override
            {
                result = "lib::str";
            }

            void VisitEnum(const EchoFieldEnum& field) override
            {
                result = field.type->qualifiedDetailName;
            }

            void VisitSFixed64(const EchoFieldSFixed64& field) override
            {
                result = "int64_t";
            }

            void VisitSFixed32(const EchoFieldSFixed32& field) override
            {
                result = "int32_t";
            }

            void VisitFloat(const EchoFieldFloat& /*field*/) override
            {
                result = "float";
            }
            
            void VisitDouble(const EchoFieldDouble& /*field*/) override
            {
                result = "double";
            }

            void VisitMessage(const EchoFieldMessage& field) override
            {
                result = field.message->qualifiedDetailName;
            }

            void VisitBytes(const EchoFieldBytes& field) override
            {
                // result = "infra::BoundedVector<uint8_t>::WithMaxSize<" + SimpleItoa(field.maxBytesSize) + ">";
                result = "lib::InlineString<" + SimpleItoa(field.maxBytesSize) + ">";
            }

            void VisitUnboundedBytes(const EchoFieldUnboundedBytes& field) override
            {
                result = "std::vector<uint8_t>";
            }

            void VisitUint32(const EchoFieldUint32& field) override
            {
                result = "uint32_t";
            }

            void VisitOptional(const EchoFieldOptional& field) override
            {
                std::string r;
                StorageTypeVisitor visitor(r);
                field.type->Accept(visitor);
                result = "infra::Optional<" + r + ">";
            }

            void VisitRepeated(const EchoFieldRepeated& field) override
            {
                std::string r;
                StorageTypeVisitor visitor(r);
                field.type->Accept(visitor);
                result = "infra::BoundedVector<" + r + ">::WithMaxSize<" + SimpleItoa(field.maxArraySize) + ">";
            }

            void VisitUnboundedRepeated(const EchoFieldUnboundedRepeated& field) override
            {
                std::string r;
                StorageTypeVisitor visitor(r);
                field.type->Accept(visitor);
                result = "std::vector<" + r + ">";
            }

        protected:
            std::string& result;
        };

        class ReferenceStorageTypeVisitor
            : public StorageTypeVisitor
        {
        public:
            using StorageTypeVisitor::StorageTypeVisitor;

            void VisitString(const EchoFieldString& field) override
            {
                result = "infra::BoundedConstString";
            }

            void VisitMessage(const EchoFieldMessage& field) override
            {
                result = field.message->qualifiedDetailReferenceName;
            }

            void VisitBytes(const EchoFieldBytes& field) override
            {
                result = "infra::ConstByteRange";
            }

            void VisitEnum(const EchoFieldEnum& field) override
            {
                result = field.type->name;
            }

            void VisitOptional(const EchoFieldOptional& field) override
            {
                std::string r;
                ReferenceStorageTypeVisitor visitor(r);
                field.type->Accept(visitor);
                result = "infra::Optional<" + r + ">";
            }

            void VisitRepeated(const EchoFieldRepeated& field) override
            {
                std::string r;
                ReferenceStorageTypeVisitor visitor(r);
                field.type->Accept(visitor);
                result = "infra::BoundedVector<" + r + ">::WithMaxSize<" + SimpleItoa(field.maxArraySize) + ">";
            }
        };

        class ReferenceDetailStorageTypeVisitor
            : public ReferenceStorageTypeVisitor
        {
        public:
            using ReferenceStorageTypeVisitor::ReferenceStorageTypeVisitor;

            void VisitMessage(const EchoFieldMessage& field) override
            {
                result = field.message->qualifiedDetailReferenceName;
            }

            void VisitEnum(const EchoFieldEnum& field) override
            {
                result = field.type->qualifiedDetailName;
            }
        };

        class ParameterTypeVisitor
            : public StorageTypeVisitor
        {
        public:
            using StorageTypeVisitor::StorageTypeVisitor;

            void VisitString(const EchoFieldString& field) override
            {
                result = "lib::str";
            }

            void VisitUnboundedString(const EchoFieldUnboundedString& field) override
            {
                result = "const std::string&";
            }

            void VisitMessage(const EchoFieldMessage& field) override
            {
                result = "const " + field.message->qualifiedName + "&";
            }

            void VisitBytes(const EchoFieldBytes& field) override
            {
                result = "infra::ConstByteRange";
            }

            void VisitUnboundedBytes(const EchoFieldUnboundedBytes& field) override
            {
                result = "infra::ConstByteRange";
            }

            void VisitEnum(const EchoFieldEnum& field) override
            {
                result = field.type->qualifiedDetailName;
            }

            void VisitRepeated(const EchoFieldRepeated& field) override
            {
                std::string r;
                StorageTypeVisitor visitor(r);
                field.type->Accept(visitor);
                result = "infra::MemoryRange<const " + r + ">";
            }

            void VisitUnboundedRepeated(const EchoFieldUnboundedRepeated& field) override
            {
                if (field.type->protoType == "services::ProtoBool")
                    result = "const std::vector<bool>&";
                else
                {
                    std::string r;
                    StorageTypeVisitor visitor(r);
                    field.type->Accept(visitor);
                    result = "infra::MemoryRange<const " + r + ">";
                }
            }
        };

        class ParameterDetailTypeVisitor
            : public ParameterTypeVisitor
        {
        public:
            using ParameterTypeVisitor::ParameterTypeVisitor;

            void VisitMessage(const EchoFieldMessage& field) override
            {
                result = "const " + field.message->qualifiedDetailName + "&";
            }
        };

        class ParameterReferenceTypeVisitor
            : public ParameterTypeVisitor
        {
        public:
            using ParameterTypeVisitor::ParameterTypeVisitor;

            void VisitMessage(const EchoFieldMessage& field) override
            {
                result = "const " + field.message->qualifiedDetailReferenceName + "&";
            }

            void VisitBytes(const EchoFieldBytes& field) override
            {
                result = "infra::ConstByteRange";
            }

            void VisitOptional(const EchoFieldOptional& field) override
            {
                std::string r;
                ReferenceStorageTypeVisitor visitor(r);
                field.type->Accept(visitor);
                result = "infra::Optional<" + r + ">";
            }

            void VisitRepeated(const EchoFieldRepeated& field) override
            {
                std::string r;
                ReferenceStorageTypeVisitor visitor(r);
                field.type->Accept(visitor);
                result = "infra::MemoryRange<const " + r + ">";
            }
        };

        class FieldNameVisitor
            : public EchoFieldVisitor
        {
        public:
            explicit FieldNameVisitor(std::string& result)
                : result(result)
            {}

            void VisitInt64(const EchoFieldInt64& field) override
            {
                result = field.name;
            }

            void VisitUint64(const EchoFieldUint64& field) override
            {
                result = field.name;
            }

            void VisitInt32(const EchoFieldInt32& field) override
            {
                result = field.name;
            }

            void VisitFixed64(const EchoFieldFixed64& field) override
            {
                result = field.name;
            }

            void VisitFixed32(const EchoFieldFixed32& field) override
            {
                result = field.name;
            }

            void VisitFloat(const EchoFieldFloat& field) override
            {
                result = field.name;
            }

            void VisitDouble(const EchoFieldDouble& field) override
            {
                result = field.name;
            }

            void VisitBool(const EchoFieldBool& field) override
            {
                result = field.name;
            }

            void VisitString(const EchoFieldString& field) override
            {
                result = field.name;
            }

            void VisitUnboundedString(const EchoFieldUnboundedString& field) override
            {
                result = field.name;
            }

            void VisitEnum(const EchoFieldEnum& field) override
            {
                result = field.name;
            }

            void VisitSFixed64(const EchoFieldSFixed64& field) override
            {
                result = field.name;
            }

            void VisitSFixed32(const EchoFieldSFixed32& field) override
            {
                result = field.name;
            }

            void VisitMessage(const EchoFieldMessage& field) override
            {
                result = field.name;
            }

            void VisitBytes(const EchoFieldBytes& field) override
            {
                result = field.name;
            }

            void VisitUnboundedBytes(const EchoFieldUnboundedBytes& field) override
            {
                result = field.name;
            }

            void VisitUint32(const EchoFieldUint32& field) override
            {
                result = field.name;
            }

            void VisitOptional(const EchoFieldOptional& field) override
            {
                result = field.name;
            }

        protected:
            std::string& result;
        };

        class DecayedReferenceVisitor
            : public FieldNameVisitor
        {
        public:
            using FieldNameVisitor::FieldNameVisitor;

            void VisitRepeated(const EchoFieldRepeated& field) override
            {
                result = "infra::MakeRange(" + field.name + ")";
            }

            void VisitUnboundedRepeated(const EchoFieldUnboundedRepeated& field) override
            {
                if (field.type->protoType == "services::ProtoBool")
                    result = field.name;
                else
                    result = "infra::MakeRange(" + field.name + ")";
            }
        };

        class DecayedVisitor
            : public DecayedReferenceVisitor
        {
        public:
            using DecayedReferenceVisitor::DecayedReferenceVisitor;

            void VisitBytes(const EchoFieldBytes& field) override
            {
                result = "infra::MakeRange(" + field.name + ")";
            }

            void VisitUnboundedBytes(const EchoFieldUnboundedBytes& field) override
            {
                result = "infra::MakeRange(" + field.name + ")";
            }
        };

        class InitializerVisitor
            : public FieldNameVisitor
        {
        public:
            using FieldNameVisitor::FieldNameVisitor;

            void VisitBytes(const EchoFieldBytes& field) override
            {
                result = field.name + ".begin(), " + field.name + ".end()";
            }

            void VisitUnboundedBytes(const EchoFieldUnboundedBytes& field) override
            {
                result = field.name + ".begin(), " + field.name + ".end()";
            }

            void VisitRepeated(const EchoFieldRepeated& field) override
            {
                result = field.name + ".begin(), " + field.name + ".end()";
            }

            void VisitUnboundedRepeated(const EchoFieldUnboundedRepeated& field) override
            {
                result = field.name + ".begin(), " + field.name + ".end()";
            }
        };
    }

    bool CppInfraCodeGenerator::Generate(const google::protobuf::FileDescriptor* file, const std::string& parameter,
        google::protobuf::compiler::GeneratorContext* generatorContext, std::string* error) const
    {

        std::vector<std::pair<std::string, std::string>> config_options;
        google::protobuf::compiler::ParseGeneratorParameter(parameter, &config_options);

        for (const auto& [key, value] : config_options) {
            if (key == "config") {
                // TODO parse config as yaml
                
            } else {
                *error = "unknown parameter '" + key + "'";
                return false;
            }
        }

        std::map<std::string, std::string> opts;
        eprint "Generating code for file %v with parameter '%v'" % file->name().data(), parameter;
        try
        {
            std::string basename = google::protobuf::compiler::cpp::StripProto(file->name()) + ".pb";

            EchoGenerator clientHeaderGenerator(generatorContext, basename, "_client.h", file, {.generate_client = true});
            clientHeaderGenerator.GenerateHeader();
            EchoGenerator clientSourceGenerator(generatorContext, basename, "_client.cc", file, {.generate_client = true});
            clientSourceGenerator.GenerateSource();

            EchoGenerator serverHeaderGenerator(generatorContext, basename, "_server.h", file, {.generate_server = true});
            serverHeaderGenerator.GenerateHeader();
            EchoGenerator serverSourceGenerator(generatorContext, basename, "_server.cc", file, {.generate_server = true});
            serverSourceGenerator.GenerateSource();

            EchoGenerator sharedHeaderGenerator(generatorContext, basename, "_msg.h", file, {.generate_shared = true});
            sharedHeaderGenerator.GenerateHeader();
            EchoGenerator sharedSourceGenerator(generatorContext, basename, "_msg.cc", file, {.generate_shared = true});
            sharedSourceGenerator.GenerateSource();

            // TracingEchoGenerator tracingHeaderGenerator(generatorContext, "Tracing" + basename + ".hpp", file);
            // tracingHeaderGenerator.GenerateHeader();
            // TracingEchoGenerator tracingSourceGenerator(generatorContext, "Tracing" + basename + ".cpp", file);
            // tracingSourceGenerator.GenerateSource();

            return true;
        }
        catch (UnsupportedFieldType& exception)
        {
            *error = "Unsupported field type " + SimpleItoa(exception.type) + " of field " + exception.fieldName;
            return false;
        }
        catch (UnspecifiedStringSize& exception)
        {
            *error = "Field " + exception.fieldName + " needs a string_size specifying its maximum number of characters";
            return false;
        }
        catch (UnspecifiedBytesSize& exception)
        {
            *error = "Field " + exception.fieldName + " needs a bytes_size specifying its maximum number of bytes";
            return false;
        }
        catch (UnspecifiedArraySize& exception)
        {
            *error = "Field " + exception.fieldName + " needs an array_size specifying its maximum number of elements";
            return false;
        }
        catch (UnspecifiedServiceId& exception)
        {
            *error = "Field " + exception.service + " needs a service_id specifying its id";
            return false;
        }
        catch (UnspecifiedMethodId& exception)
        {
            *error = "Field " + exception.service + "." + exception.method + " needs a method_id specifying its id";
            return false;
        }
        catch (MessageNotFound& exception)
        {
            *error = "Message " + exception.name + " was used before having been fully defined";
            return false;
        }
        catch (EnumNotFound& exception)
        {
            *error = "Enum " + exception.name + " was used before having been fully defined";
            return false;
        }
    }

    uint64_t CppInfraCodeGenerator::GetSupportedFeatures() const
    {
        return FEATURE_PROTO3_OPTIONAL;
    }

    EnumGenerator::EnumGenerator(const std::shared_ptr<const EchoEnum>& enum_)
        : enum_(enum_)
    {}

    void EnumGenerator::Run(Entities& formatter)
    {
        formatter.Add(std::make_shared<EnumDeclaration>(enum_->name, enum_->members, true));
    }

    MessageEnumGenerator::MessageEnumGenerator(const std::shared_ptr<const EchoMessage>& message)
        : message(message)
    {}

    void MessageEnumGenerator::Run(Entities& formatter)
    {
        if (!message->nestedEnums.empty())
        {
            auto enumNamespace = std::make_shared<Namespace>("detail1");

            for (auto& nestedEnum : message->nestedEnums)
                enumNamespace->Add(std::make_shared<EnumDeclaration>(message->name + nestedEnum->name, nestedEnum->members, /*scoped*/ false));

            formatter.Add(enumNamespace);
        }
    }

    MessageTypeMapGenerator::MessageTypeMapGenerator(const std::shared_ptr<const EchoMessage>& message, const std::string& prefix)
        : message(message)
        , prefix(prefix)
    {}

    void MessageTypeMapGenerator::Run(Entities& formatter) const
    {
        auto typeMapNamespace = std::make_shared<Namespace>("detail2");

        auto typeMapDeclaration = std::make_shared<StructTemplateForwardDeclaration>(MessageName() + "TypeMap");
        typeMapDeclaration->TemplateParameter("std::size_t fieldIndex");
        typeMapNamespace->Add(typeMapDeclaration);

        for (auto& field : message->fields)
        {
            auto typeMapSpecialization = std::make_shared<StructTemplateSpecialization>(MessageName() + "TypeMap");
            typeMapSpecialization->TemplateSpecialization(SimpleItoa(std::distance(message->fields.data(), &field)));
            AddTypeMapProtoType(*field, *typeMapSpecialization);
            AddTypeMapType(*field, *typeMapSpecialization);
            AddTypeMapDecayedType(*field, *typeMapSpecialization);
            AddTypeMapFieldNumber(*field, *typeMapSpecialization);
            typeMapNamespace->Add(typeMapSpecialization);
        }

        formatter.Add(typeMapNamespace);
    }

    void MessageTypeMapGenerator::AddTypeMapProtoType(const EchoField& field, Entities& entities) const
    {
        entities.Add(std::make_shared<Using>("ProtoType", field.protoType));
    }

    void MessageTypeMapGenerator::AddTypeMapType(const EchoField& field, Entities& entities) const
    {
        std::string result;
        StorageTypeVisitor visitor(result);
        field.Accept(visitor);
        entities.Add(std::make_shared<Using>("Type", result));
    }

    void MessageTypeMapGenerator::AddTypeMapDecayedType(const EchoField& field, Entities& entities) const
    {
        std::string result;
        ParameterDetailTypeVisitor visitor(result);
        field.Accept(visitor);
        entities.Add(std::make_shared<Using>("DecayedType", result));
    }

    void MessageTypeMapGenerator::AddTypeMapFieldNumber(const EchoField& field, Entities& entities) const
    {
        entities.Add(std::make_shared<DataMember>("fieldNumber", "static const uint32_t", SimpleItoa(field.number)));
    }

    std::string MessageTypeMapGenerator::MessageName() const
    {
        return prefix + message->name + MessageSuffix();
    }

    std::string MessageTypeMapGenerator::MessageSuffix() const
    {
        return "";
    }

    void MessageReferenceTypeMapGenerator::AddTypeMapProtoType(const EchoField& field, Entities& entities) const
    {
        entities.Add(std::make_shared<Using>("ProtoType", field.protoReferenceType));
    }

    void MessageReferenceTypeMapGenerator::AddTypeMapType(const EchoField& field, Entities& entities) const
    {
        std::string result;
        ReferenceDetailStorageTypeVisitor visitor(result);
        field.Accept(visitor);
        entities.Add(std::make_shared<Using>("Type", result));
    }

    void MessageReferenceTypeMapGenerator::AddTypeMapDecayedType(const EchoField& field, Entities& entities) const
    {
        std::string result;
        ParameterReferenceTypeVisitor visitor(result);
        field.Accept(visitor);
        entities.Add(std::make_shared<Using>("DecayedType", result));
    }

    std::string MessageReferenceTypeMapGenerator::MessageSuffix() const
    {
        return "Reference";
    }

    MessageGenerator::MessageGenerator(const std::shared_ptr<const EchoMessage>& message, const std::string& prefix)
        : message(message)
        , prefix(prefix)
    {}

    void MessageGenerator::Run(Entities& formatter)
    {
        // GenerateNestedMessages(formatter);
        // GenerateTypeMap(formatter);
        GenerateClass(formatter);
        // GenerateNestedMessageAliases();
        GenerateEnums();
        // GenerateConstructors();
        GenerateFunctions();
        // GenerateTypeMap();
        // GenerateGetters();
        GenerateFieldDeclarations();
        GenerateFieldConstants();
        // GenerateFieldSizes();
        // GenerateMaxMessageSize();
    }

    void MessageGenerator::GenerateTypeMap(Entities& formatter)
    {
        MessageTypeMapGenerator typeMapGenerator(message, prefix);
        typeMapGenerator.Run(formatter);
    }

    void MessageGenerator::GenerateClass(Entities& formatter)
    {
        auto struct_ = std::make_shared<Struct>(ClassName());
        structFormatter = struct_.get();
        formatter.Add(struct_);
    }

    void MessageGenerator::GenerateConstructors()
    {
        // auto constructors = std::make_shared<Access>("public");
        structFormatter->Add(std::make_shared<Constructor>(ClassName(), "", Constructor::cDefault));

        // if (!message->fields.empty())
        // {
        //     auto constructByMembers = std::make_shared<Constructor>(ClassName(), "", 0);
        //     std::string typeNameResult;
        //     ParameterTypeVisitor visitor(typeNameResult);
        //     std::string initializer;
        //     InitializerVisitor initializerVisitor(initializer);
        //     for (auto& field : message->fields)
        //     {
        //         field->Accept(visitor);
        //         constructByMembers->Parameter(typeNameResult + " " + field->name);
        //         field->Accept(initializerVisitor);
        //         constructByMembers->Initializer(field->name + "(" + initializer + ")");
        //     }
        //     structFormatter->Add(constructByMembers);
        // }

        auto constructByProtoParser = std::make_shared<Constructor>(ClassName(), "this->unmarshal(data);\n", 0);
        // constructByProtoParser->Parameter("infra::ProtoParser& parser");
        constructByProtoParser->Parameter("lib::str data");
        structFormatter->Add(constructByProtoParser);
        // structFormatter->Add(constructors);
    }

    void MessageGenerator::GenerateFunctions()
    {
        // auto functions = std::make_shared<Access>("public");

        auto serialize = std::make_shared<Function>("marshal", SerializerBody(), "void", Function::fStatic);
        serialize->Parameter(ClassName() + " const &req");
        serialize->Parameter("lib::io::Writer &out");
        serialize->Parameter("lib::error err");
        serialize->Parameter("int nesting");
        serialize->Parameter("serialrpc::Stack &stack");
        structFormatter->Add(serialize);

        // auto deserialize = std::make_shared<Function>("Deserialize", DeserializerBody(), "void", 0);
        // deserialize->Parameter("infra::ProtoParser& parser");
        // functions->Add(deserialize);
        // auto compareUnEqual = std::make_shared<Function>("operator!=", CompareUnEqualBody(), "bool", Function::fConst);
        // compareUnEqual->Parameter("const " + ClassName() + "& other");
        // functions->Add(compareUnEqual);

        // structFormatter->Add(functions);

        auto unmarshal = std::make_shared<Function>("unmarshal", DeserializerBody(), ClassName(), Function::fStatic);
        unmarshal->Parameter("lib::io::Reader &in");
        unmarshal->Parameter("lib::error err");
        unmarshal->Parameter("int nesting", "128");
        structFormatter->Add(unmarshal);

        auto compareEqual = std::make_shared<Function>("operator==", CompareEqualBody(), "bool", Function::fConst);
        compareEqual->Parameter("const " + ClassName() + "& other");
        structFormatter->Add(compareEqual);
    }

    void MessageGenerator::GenerateTypeMap()
    {
        auto typeMap = std::make_shared<Access>("public");

        auto numberOfFields = std::make_shared<DataMember>("numberOfFields", "static const uint32_t", SimpleItoa(message->fields.size()));
        typeMap->Add(numberOfFields);
        auto protoTypeUsing = std::make_shared<UsingTemplate>("ProtoType", "typename " + TypeMapName() + "<fieldIndex>::ProtoType");
        protoTypeUsing->TemplateParameter("std::size_t fieldIndex");
        typeMap->Add(protoTypeUsing);
        auto typeUsing = std::make_shared<UsingTemplate>("Type", "typename " + TypeMapName() + "<fieldIndex>::Type");
        typeUsing->TemplateParameter("std::size_t fieldIndex");
        typeMap->Add(typeUsing);
        auto decayedTypeUsing = std::make_shared<UsingTemplate>("DecayedType", "typename " + TypeMapName() + "<fieldIndex>::DecayedType");
        decayedTypeUsing->TemplateParameter("std::size_t fieldIndex");
        typeMap->Add(decayedTypeUsing);
        auto fieldNumber = std::make_shared<DataMember>("fieldNumber", "template<std::size_t fieldIndex> static const uint32_t", TypeMapName() + "<fieldIndex>::fieldNumber");
        typeMap->Add(fieldNumber);

        structFormatter->Add(typeMap);
    }

    void MessageGenerator::GenerateGetters()
    {
        auto getters = std::make_shared<Access>("public");

        for (auto& field : message->fields)
        {
            auto index = std::distance(message->fields.data(), &field);
            auto functionGet = std::make_shared<Function>("Get", "return " + field->name + ";\n", ClassName() + "::Type<" + SimpleItoa(index) + ">&", 0);
            functionGet->Parameter("std::integral_constant<uint32_t, " + SimpleItoa(index) + ">");
            getters->Add(functionGet);
            auto functionConstGet = std::make_shared<Function>("Get", "return " + field->name + ";\n", "const " + ClassName() + "::Type<" + SimpleItoa(index) + ">&", Function::fConst);
            functionConstGet->Parameter("std::integral_constant<uint32_t, " + SimpleItoa(index) + ">");
            getters->Add(functionConstGet);
            std::string result;
            DecayedVisitor visitor(result);
            field->Accept(visitor);
            auto functionConstGetDecayed = std::make_shared<Function>("GetDecayed", "return " + result + ";\n", ClassName() + "::DecayedType<" + SimpleItoa(index) + ">", Function::fConst);
            functionConstGetDecayed->Parameter("std::integral_constant<uint32_t, " + SimpleItoa(index) + ">");
            getters->Add(functionConstGetDecayed);
        }

        structFormatter->Add(getters);
    }

    void MessageGenerator::GenerateNestedMessageAliases()
    {
        if (!message->nestedMessages.empty())
        {
            auto aliases = std::make_shared<Access>("public");

            for (auto& nestedMessage : message->nestedMessages)
                aliases->Add(std::make_shared<Using>(nestedMessage->name + MessageSuffix(), nestedMessage->qualifiedDetailName + MessageSuffix()));

                structFormatter->Add(aliases);
        }
    }

    void MessageGenerator::GenerateEnums()
    {
        if (!message->nestedEnums.empty())
        {
            // auto aliases = std::make_shared<Access>("public");

            for (auto& nestedEnum : message->nestedEnums) {
                //aliases->Add(std::make_shared<Using>(nestedEnum->name, ReferencedEnumPrefix() + nestedEnum->name));
                //structFormatter->Add(std::make_shared<EnumDeclaration>(message->name + nestedEnum->name, nestedEnum->members, /* scoped */ false));
                structFormatter->Add(std::make_shared<EnumDeclaration>(nestedEnum->name, nestedEnum->members, /* scoped */ false));
            }

            // structFormatter->Add(aliases);
        }
    }

    void MessageGenerator::GenerateNestedMessages(Entities& formatter)
    {
        if (!message->nestedMessages.empty())
        {
            auto nestedMessages = std::make_shared<Namespace>("detail3");

            for (auto& nestedMessage : message->nestedMessages)
                MessageGenerator(nestedMessage, ClassName()).Run(*nestedMessages);

            formatter.Add(nestedMessages);
        }
    }

    void MessageGenerator::GenerateFieldDeclarations()
    {
        if (!message->fields.empty())
        {
            // auto fields = std::make_shared<Access>("public");

            std::string result;
            StorageTypeVisitor visitor(result);
            for (auto& field : message->fields)
            {
                field->Accept(visitor);
                structFormatter->Add(std::make_shared<DataMember>(field->name, result, "{}"));
            }

            // structFormatter->Add(fields);
        }
    }

    void MessageGenerator::GenerateFieldConstants()
    {
        if (!message->fields.empty())
        {
            // auto fields = std::make_shared<Access>("public");

            for (auto& field : message->fields)
                structFormatter->Add(std::make_shared<DataMember>(field->constantName, "static const uint32_t", SimpleItoa(field->number)));

                // structFormatter->Add(fields);
        }
    }

    void MessageGenerator::GenerateFieldSizes()
    {
        class FieldSizeVisitor
            : public EchoFieldVisitor
        {
        public:
            explicit FieldSizeVisitor(bool& added, Entities& entities)
                : added(added)
                , entities(entities)
            {}

            void VisitInt64(const EchoFieldInt64& field) override
            {}

            void VisitUint64(const EchoFieldUint64& field) override
            {}

            void VisitInt32(const EchoFieldInt32& field) override
            {}

            void VisitFixed64(const EchoFieldFixed64& field) override
            {}

            void VisitFixed32(const EchoFieldFixed32& field) override
            {}

            void VisitBool(const EchoFieldBool& field) override
            {}

            void VisitString(const EchoFieldString& field) override
            {
                added = true;
                entities.Add(std::make_shared<DataMember>(field.name + "Size", "static constexpr uint32_t", SimpleItoa(field.maxStringSize)));
            }

            void VisitUnboundedString(const EchoFieldUnboundedString& field) override
            {}

            void VisitEnum(const EchoFieldEnum& field) override
            {}

            void VisitSFixed64(const EchoFieldSFixed64& field) override
            {}

            void VisitSFixed32(const EchoFieldSFixed32& field) override
            {}

            void VisitFloat(const EchoFieldFloat& /*field*/) override
            {}

            void VisitDouble(const EchoFieldDouble& /*field*/) override
            {}

            void VisitMessage(const EchoFieldMessage& field) override
            {}

            void VisitBytes(const EchoFieldBytes& field) override
            {
                added = true;
                entities.Add(std::make_shared<DataMember>(field.name + "Size", "static constexpr uint32_t", SimpleItoa(field.maxBytesSize)));
            }

            void VisitUnboundedBytes(const EchoFieldUnboundedBytes& field) override
            {}

            void VisitUint32(const EchoFieldUint32& field) override
            {}

            void VisitOptional(const EchoFieldOptional& field) override
            {
                FieldSizeVisitor visitor(added, entities);
                field.type->Accept(visitor);
            }

            void VisitRepeated(const EchoFieldRepeated& field) override
            {
                added = true;
                entities.Add(std::make_shared<DataMember>(field.name + "Size", "static constexpr uint32_t", SimpleItoa(field.maxArraySize)));
            }

            void VisitUnboundedRepeated(const EchoFieldUnboundedRepeated& field) override
            {}

        private:
            bool& added;
            Entities& entities;
        };

        auto fields = std::make_shared<Access>("public");
        bool added = false;

        for (auto& field : message->fields)
        {
            FieldSizeVisitor visitor(added, *fields);
            field->Accept(visitor);
        }

        if (added)
            structFormatter->Add(fields);
    }

    void MessageGenerator::GenerateMaxMessageSize()
    {
        if (message->MaxMessageSize() != std::nullopt)
        {
            auto fields = std::make_shared<Access>("public");
            fields->Add(std::make_shared<DataMember>("maxMessageSize", "static const uint32_t", SimpleItoa(*message->MaxMessageSize())));
            structFormatter->Add(fields);
        }
    }

    std::string MessageGenerator::SerializerBody()
    {
        std::ostringstream result;
        {
            google::protobuf::io::OstreamOutputStream stream(&result);
            google::protobuf::io::Printer::Options options('$', nullptr);
            options.spaces_per_indent = 4;
            google::protobuf::io::Printer printer(&stream, options);

            for (auto& field : message->fields) {
                // printer.Print("SerializeField($type$(), formatter, $name$, $constant$);\n", "type", field->protoType, "name", field->name, "constant", field->constantName);
                if (field->type == google::protobuf::FieldDescriptor::TYPE_ENUM) {
                    printer.Print("serialrpc::marshal_field(out, req.$id$, int32(req.$name$), err, nesting-1, stack);\n", "id", field->constantName, "name", field->name);
                } else {
                    printer.Print("serialrpc::marshal_field(out, req.$id$, req.$name$, err, nesting-1, stack);\n", "id", field->constantName, "name", field->name);
                }
                printer.Print("if (err) {\n    return;\n}\n");
            }

            if (message->fields.empty()) {
                printer.Print("(void) req;\n");
                printer.Print("(void) out;\n");
                printer.Print("(void) err;\n");
                printer.Print("(void) nesting;\n");
                printer.Print("(void) stack;\n");
            }

        }

        return result.str();
    }

    std::string MessageGenerator::DeserializerBody()
    {
        std::ostringstream result;
        {
            google::protobuf::io::OstreamOutputStream stream(&result);
            google::protobuf::io::Printer::Options options('$', nullptr);
            options.spaces_per_indent = 4;
            google::protobuf::io::Printer printer(&stream, options);
            
            printer.Print(std::string_view("$type$ msg;\n"), "type", ClassName());
            printer.Print("\n");
            printer.Print("for (;;) {\n");
            
            printer.Indent();
            printer.Print("serialrpc::Tag tag = serialrpc::read_tag(in, err);\n");
            printer.Print("if (err) {\n    return msg;\n}\n\n");
            printer.Print("if (tag.type == serialrpc::Tag::End) {\n    return msg;\n}\n\n");
            printer.Print("switch (tag.field_num) {\n");

            for (auto& field : message->fields) {
                printer.Print("case $constant$:\n", "constant", field->constantName);
                printer.Indent();

                std::string type;
                StorageTypeVisitor visitor(type);
                field->Accept(visitor);

                if (field->type == google::protobuf::FieldDescriptor::TYPE_ENUM) {
                    printer.Print("msg.$name$ = ($type$) serialrpc::unmarshal<int32>(in, err, nesting-1);\n", "name", field->name, "type", type);
                } else {
                    printer.Print("msg.$name$ = serialrpc::unmarshal<$type$>(in, err, nesting-1);\n", "name", field->name, "type", type);
                }
                
                printer.Print("break;\n\n");
                printer.Outdent();
            }
            
            printer.Print("default:\n");
            printer.Indent();
            printer.Print("serialrpc::skip(in, tag.type, err, nesting-1);\n");
            printer.Print("if (err) {\n    return msg;\n}\n");;
            printer.Outdent();
            printer.Print("}\n");
            printer.Outdent();
            
            printer.Print("}\n\n");
            printer.Outdent();
            printer.Print("return msg;\n");
        }

        return result.str();
    }

    std::string MessageGenerator::CompareEqualBody() const
    {
        std::ostringstream result;
        {
            google::protobuf::io::OstreamOutputStream stream(&result);
            google::protobuf::io::Printer::Options options('$', nullptr);
            options.spaces_per_indent = 4;
            google::protobuf::io::Printer printer(&stream, options);

            // printer.Print("return true");
            bool first = true;

            // printer.Indent();
            for (auto& field : message->fields) {
                if (first) {
                    printer.Print("return $name$ == other.$name$", "name", field->name);
                    first = false;
                    continue;
                }
                printer.Print("\n    && $name$ == other.$name$", "name", field->name);
            }

            if (first) {
                printer.Print("(void) other;\n");
                printer.Print("return true");
            }
            // printer.Outdent();

            printer.Print(";\n");
        }

        return result.str();
    }

    std::string MessageGenerator::CompareUnEqualBody() const
    {
        std::ostringstream result;
        {
            google::protobuf::io::OstreamOutputStream stream(&result);
            google::protobuf::io::Printer printer(&stream, '$', nullptr);

            printer.Print("return !(*this == other);\n");
        }

        return result.str();
    }

    std::string MessageGenerator::ClassName() const
    {
        return prefix + message->name;
    }

    std::string MessageGenerator::ReferencedName() const
    {
        return ClassName();
    }

    std::string MessageGenerator::MessageSuffix() const
    {
        return "";
    }

    std::string MessageGenerator::TypeMapName() const
    {
        return "detail::" + ClassName() + "TypeMap";
    }

    std::string MessageGenerator::ReferencedEnumPrefix() const
    {
        return "detail::" + prefix + message->name;
    }

    void MessageReferenceGenerator::GenerateTypeMap(Entities& formatter)
    {
        MessageReferenceTypeMapGenerator typeMapGenerator(message, prefix);
        typeMapGenerator.Run(formatter);
    }

    void MessageReferenceGenerator::GenerateConstructors()
    {
        auto constructors = std::make_shared<Access>("public");
        constructors->Add(std::make_shared<Constructor>(ClassName(), "", Constructor::cDefault));

        if (!message->fields.empty())
        {
            auto constructByMembers = std::make_shared<Constructor>(ClassName(), "", 0);
            std::string result;
            ParameterReferenceTypeVisitor visitor(result);
            std::string initializer;
            InitializerVisitor initializerVisitor(initializer);
            for (auto& field : message->fields)
            {
                field->Accept(visitor);
                field->Accept(initializerVisitor);
                constructByMembers->Parameter(result + " " + field->name);
                constructByMembers->Initializer(field->name + "(" + initializer + ")");
            }
            constructors->Add(constructByMembers);
        }

        auto constructByProtoParser = std::make_shared<Constructor>(ClassName(), "Deserialize(parser);\n", 0);
        constructByProtoParser->Parameter("infra::ProtoParser& parser");
        constructors->Add(constructByProtoParser);
        structFormatter->Add(constructors);
    }

    void MessageReferenceGenerator::GenerateGetters()
    {
        auto getters = std::make_shared<Access>("public");

        for (auto& field : message->fields)
        {
            auto index = std::distance(message->fields.data(), &field);
            auto functionGet = std::make_shared<Function>("Get", "return " + field->name + ";\n", ClassName() + "::Type<" + SimpleItoa(index) + ">&", 0);
            functionGet->Parameter("std::integral_constant<uint32_t, " + SimpleItoa(index) + ">");
            getters->Add(functionGet);
            auto functionConstGet = std::make_shared<Function>("Get", "return " + field->name + ";\n", ClassName() + "::Type<" + SimpleItoa(index) + ">", Function::fConst);
            functionConstGet->Parameter("std::integral_constant<uint32_t, " + SimpleItoa(index) + ">");
            getters->Add(functionConstGet);
            std::string result;
            DecayedReferenceVisitor visitor(result);
            field->Accept(visitor);
            auto functionConstGetDecayed = std::make_shared<Function>("GetDecayed", "return " + result + ";\n", ClassName() + "::DecayedType<" + SimpleItoa(index) + ">", Function::fConst);
            functionConstGetDecayed->Parameter("std::integral_constant<uint32_t, " + SimpleItoa(index) + ">");
            getters->Add(functionConstGetDecayed);
        }

        structFormatter->Add(getters);
    }

    void MessageReferenceGenerator::GenerateNestedMessages(Entities& formatter)
    {
        if (!message->nestedMessages.empty())
        {
            auto nestedMessages = std::make_shared<Namespace>("detail");

            for (auto& nestedMessage : message->nestedMessages)
                MessageReferenceGenerator(nestedMessage, message->name).Run(*nestedMessages);

            formatter.Add(nestedMessages);
        }
    }

    void MessageReferenceGenerator::GenerateFieldDeclarations()
    {
        if (!message->fields.empty())
        {
            auto fields = std::make_shared<Access>("public");

            std::string result;
            ReferenceStorageTypeVisitor visitor(result);
            for (auto& field : message->fields)
            {
                field->Accept(visitor);
                fields->Add(std::make_shared<DataMember>(field->name, result, "{}"));
            }

            structFormatter->Add(fields);
        }
    }

    void MessageReferenceGenerator::GenerateMaxMessageSize()
    {}

    std::string MessageReferenceGenerator::SerializerBody()
    {
        return "std::abort();\n";
    }

    std::string MessageReferenceGenerator::ClassName() const
    {
        return MessageGenerator::ClassName() + "Reference";
    }

    std::string MessageReferenceGenerator::ReferencedName() const
    {
        return "detail::" + ClassName();
    }

    std::string MessageReferenceGenerator::MessageSuffix() const
    {
        return "Reference";
    }

    ServiceGenerator::ServiceGenerator(const std::shared_ptr<const EchoService>& service, Entities& formatter)
        : service(service)
    {
        auto serviceClass = std::make_shared<Struct>(service->name);
        // serviceClass->Parent("public services::Service");
        serviceFormatter = serviceClass.get();
        formatter.Add(serviceClass);

        // auto serviceProxyClass = std::make_shared<Class>(service->name + "Proxy");
        // serviceProxyClass->Parent("public services::ServiceProxy");
        // serviceProxyFormatter = serviceProxyClass.get();
        // formatter.Add(serviceProxyClass);

        // GenerateServiceConstructors();
        // GenerateServiceProxyConstructors();
        GenerateServiceFunctions();
        // GenerateServiceProxyFunctions();
        // GenerateFieldConstants();
        // GenerateMethodTypeList();
    }

    void ServiceGenerator::GenerateServiceConstructors()
    {
        auto constructors = std::make_shared<Access>("public");
        auto constructor = std::make_shared<Constructor>(service->name, "", 0);
        constructor->Parameter("services::Echo& echo");
        constructor->Initializer("services::Service(echo)");

        constructors->Add(constructor);
        serviceFormatter->Add(constructors);
    }

    void ServiceGenerator::GenerateServiceProxyConstructors()
    {
        auto constructors = std::make_shared<Access>("public");
        auto constructor = std::make_shared<Constructor>(service->name + "Proxy", "", 0);
        constructor->Parameter("services::Echo& echo");
        constructor->Initializer("services::ServiceProxy(echo, maxMessageSize)");

        constructors->Add(constructor);
        serviceProxyFormatter->Add(constructors);
    }

    void ServiceGenerator::GenerateServiceFunctions()
    {
        // auto functions = std::make_shared<Access>("public");
        

        for (auto& method : service->methods)
        {
            

            if (method.server_streaming) {
                auto serviceMethod = std::make_shared<Function>("subscribe_" + method.name, "", "void", Function::fVirtual | Function::fAbstract);
            
                serviceMethod->Parameter("RPCServer &server");
                if (method.parameter) {
                    serviceMethod->Parameter(method.parameter->name + " const &req");
                }
                serviceMethod->Parameter("lib::error err");
                serviceFormatter->Add(serviceMethod);

                serviceMethod = std::make_shared<Function>("unsubscribe_" + method.name, "", "void", Function::fVirtual | Function::fAbstract);
                // serviceMethod->Parameter("lib::error err");
            
                serviceFormatter->Add(serviceMethod);
            } else {
                std::string rettype;
                if (method.result) {
                    rettype = method.result->name;
                } else {
                    rettype = "void";
                }
                auto serviceMethod = std::make_shared<Function>(method.name, "", rettype, Function::fVirtual | Function::fAbstract);
            
                if (method.parameter) {
                    serviceMethod->Parameter(method.parameter->name + " const &req");
                }
                serviceMethod->Parameter("lib::error err");
                serviceFormatter->Add(serviceMethod);
            }
            
        }

        // auto acceptsService = std::make_shared<Function>("AcceptsService", AcceptsServiceBody(), "bool", Function::fConst | Function::fOverride);
        // acceptsService->Parameter("uint32_t id");
        // serviceFormatter->Add(acceptsService);

        // auto startMethod = std::make_shared<Function>("StartMethod", StartMethodBody(), "infra::SharedPtr<services::MethodDeserializer>", Function::fOverride);
        // startMethod->Parameter("uint32_t serviceId");
        // startMethod->Parameter("uint32_t methodId");
        // startMethod->Parameter("uint32_t size");
        // startMethod->Parameter("const services::EchoErrorPolicy& errorPolicy");
        // serviceFormatter->Add(startMethod);

        // serviceFormatter->Add(functions);
    }

    void ServiceGenerator::GenerateServiceProxyFunctions()
    {
        auto functions = std::make_shared<Access>("public");

        for (auto& method : service->methods)
        {
            auto serviceMethod = std::make_shared<Function>(method.name, ProxyMethodBody(method), "void", 0);
            if (method.parameter)
            {
                for (auto field : method.parameter->fields)
                {
                    std::string typeName;
                    ParameterTypeVisitor visitor(typeName);
                    field->Accept(visitor);
                    serviceMethod->Parameter(typeName + " " + field->name);
                }
            }

            functions->Add(serviceMethod);
        }

        serviceProxyFormatter->Add(functions);
    }

    void ServiceGenerator::GenerateFieldConstants()
    {
        auto fields = std::make_shared<Access>("public");

        fields->Add(std::make_shared<DataMember>("serviceId", "static constexpr uint32_t", SimpleItoa(service->serviceId)));

        for (auto& method : service->methods)
            fields->Add(std::make_shared<DataMember>("id" + method.name, "static constexpr uint32_t", SimpleItoa(method.methodId)));

        fields->Add(std::make_shared<DataMember>("maxMessageSize", "static constexpr uint32_t", SimpleItoa(MaxMessageSize())));

        serviceFormatter->Add(fields);
        serviceProxyFormatter->Add(fields);
    }

    void ServiceGenerator::GenerateMethodTypeList()
    {
        auto methodTypeListAccess = std::make_shared<Access>("public");

        std::string definition;

        for (auto& method : service->methods)
            if (method.parameter != nullptr)
            {
                if (!definition.empty())
                    definition += ", ";
                definition += method.parameter->qualifiedName;
            }

        methodTypeListAccess->Add(std::make_shared<Using>("MethodTypeList", "infra::List<" + definition + ">"));

        serviceFormatter->Add(methodTypeListAccess);
        serviceProxyFormatter->Add(methodTypeListAccess);
    }

    uint32_t ServiceGenerator::MaxMessageSize() const
    {
        uint32_t result = 0;

        for (auto& method : service->methods)
        {
            if (method.parameter && !method.parameter->MaxMessageSize())
                result = std::numeric_limits<uint32_t>::max();
            else if (method.parameter)
                result = std::max<uint32_t>(MaxVarIntSize(service->serviceId) + MaxVarIntSize((method.methodId << 3) | 2) + 10 + *method.parameter->MaxMessageSize(), result);
            else
                result = std::max<uint32_t>(MaxVarIntSize(service->serviceId) + MaxVarIntSize((method.methodId << 3) | 2) + 10, result);
        }

        return result;
    }

    std::string ServiceGenerator::AcceptsServiceBody() const
    {
        std::ostringstream result;
        {
            google::protobuf::io::OstreamOutputStream stream(&result);
            google::protobuf::io::Printer printer(&stream, '$', nullptr);

            printer.Print("return serviceId == id;\n");
        }

        return result.str();
    }

    std::string ServiceGenerator::StartMethodBody() const
    {
        std::ostringstream result;
        {
            google::protobuf::io::OstreamOutputStream stream(&result);
            google::protobuf::io::Printer printer(&stream, '$', nullptr);

            printer.Print(R"(switch (methodId)
{
)");

            for (auto& method : service->methods)
            {
                if (method.parameter)
                    PrintMethodCaseWithParameter(method, printer);
                else
                    printer.Print(R"(    case id$name$:
        return Rpc().SerializerFactory().MakeDeserializer<services::EmptyMessage>(infra::Function<void()>([this]() { $name$(); }));
)",
                        "name", method.name);
            }

            printer.Print(R"(    default:
        errorPolicy.MethodNotFound(serviceId, methodId);
        return Rpc().SerializerFactory().MakeDummyDeserializer(Rpc());
)");

            printer.Print("}\n");
        }

        return result.str();
    }

    void ServiceGenerator::PrintMethodCaseWithParameter(const EchoMethod& method, google::protobuf::io::Printer& printer) const
    {
        printer.Print(R"(    case id$name$:
        return Rpc().SerializerFactory().MakeDeserializer<$argument$)",
            "name", method.name, "argument", method.parameter->qualifiedName);

        for (auto& field : method.parameter->fields)
        {
            std::string typeName;
            ParameterTypeVisitor visitor(typeName);
            field->Accept(visitor);

            printer.Print(", $type$", "type", typeName);
        }

        printer.Print(">(infra::Function<void(");

        for (auto& field : method.parameter->fields)
        {
            if (&field != &method.parameter->fields.front())
                printer.Print(", ");

            std::string typeName;
            ParameterTypeVisitor visitor(typeName);
            field->Accept(visitor);

            printer.Print("$type$", "type", typeName);
        }

        printer.Print(")>([this](");

        for (auto& field : method.parameter->fields)
        {
            auto index = std::distance(&method.parameter->fields.front(), &field);
            if (index != 0)
                printer.Print(", ");

            std::string typeName;
            ParameterTypeVisitor visitor(typeName);
            field->Accept(visitor);

            printer.Print("$type$ v$index$", "type", typeName, "index", SimpleItoa(index));
        }

        printer.Print(R"() { $name$()",
            "name", method.name);

        for (auto& field : method.parameter->fields)
        {
            auto index = std::distance(&method.parameter->fields.front(), &field);
            if (index != 0)
                printer.Print(", ");

            printer.Print("v$index$", "index", SimpleItoa(index));
        }

        printer.Print(R"(); }));
)");
    }

    std::string ServiceGenerator::ProxyMethodBody(const EchoMethod& method) const
    {
        std::ostringstream result;
        {
            google::protobuf::io::OstreamOutputStream stream(&result);
            google::protobuf::io::Printer printer(&stream, '$', nullptr);

            if (method.parameter != nullptr)
            {
                printer.Print(R"(auto serializer = Rpc().SerializerFactory().MakeSerializer<$type$)", "type", method.parameter->qualifiedName);

                for (auto& field : method.parameter->fields)
                {
                    std::string typeName;
                    ParameterTypeVisitor visitor(typeName);
                    field->Accept(visitor);
                    printer.Print(", $type$", "type", typeName);
                }
            }
            else
                printer.Print(R"(auto serializer = Rpc().SerializerFactory().MakeSerializer<services::EmptyMessage)");

            printer.Print(">(serviceId, $methodId$", "methodId", SimpleItoa(method.methodId));

            if (method.parameter != nullptr)
                for (const auto& field : method.parameter->fields)
                    printer.Print(", $field$", "field", field->name);

            printer.Print(R"();
SetSerializer(serializer);
)");
        }

        return result.str();
    }

    TracingServiceGenerator::TracingServiceGenerator(const std::shared_ptr<const EchoService>& service, Entities& formatter)
        : service(service)
    {
        auto serviceClass = std::make_shared<Class>(service->name + "Tracer");
        serviceClass->Parent("public services::ServiceTracer");
        serviceFormatter = serviceClass.get();
        formatter.Add(serviceClass);

        GenerateServiceConstructors();
        GenerateServiceFunctions();
        GenerateFieldConstants();
        GenerateDataMembers();
    }

    void TracingServiceGenerator::GenerateServiceConstructors()
    {
        auto constructors = std::make_shared<Access>("public");
        auto constructor = std::make_shared<Constructor>(service->name + "Tracer", "tracingEcho.AddServiceTracer(*this);\n", 0);
        constructor->Parameter("services::TracingEchoOnStreams& tracingEcho");
        constructor->Initializer("services::ServiceTracer(serviceId)");
        constructor->Initializer("tracingEcho(tracingEcho)");
        constructors->Add(constructor);

        constructors->Add(std::make_shared<Constructor>("~" + service->name + "Tracer", "tracingEcho.RemoveServiceTracer(*this);\n", 0));

        serviceFormatter->Add(constructors);
    }

    void TracingServiceGenerator::GenerateServiceFunctions()
    {
        auto functions = std::make_shared<Access>("public");

        auto handle = std::make_shared<Function>("TraceMethod", TraceMethodBody(), "void", Function::fOverride | Function::fConst);
        handle->Parameter("uint32_t methodId");
        handle->Parameter("infra::ProtoLengthDelimited& contents");
        handle->Parameter("services::Tracer& tracer");
        functions->Add(handle);

        serviceFormatter->Add(functions);
    }

    void TracingServiceGenerator::GenerateFieldConstants()
    {
        auto fields = std::make_shared<Access>("public");

        fields->Add(std::make_shared<DataMember>("serviceId", "static const uint32_t", SimpleItoa(service->serviceId)));

        for (auto& method : service->methods)
            fields->Add(std::make_shared<DataMember>("id" + method.name, "static const uint32_t", SimpleItoa(method.methodId)));

        serviceFormatter->Add(fields);
    }

    void TracingServiceGenerator::GenerateDataMembers()
    {
        auto dataMembers = std::make_shared<Access>("public");

        dataMembers->Add(std::make_shared<DataMember>("tracingEcho", "services::TracingEchoOnStreams&"));

        serviceFormatter->Add(dataMembers);
    }

    std::string TracingServiceGenerator::TraceMethodBody() const
    {
        std::ostringstream result;
        {
            google::protobuf::io::OstreamOutputStream stream(&result);
            google::protobuf::io::Printer printer(&stream, '$', nullptr);

            if (!service->methods.empty())
            {
                printer.Print(R"(infra::ProtoParser parser(contents.Parser());

switch (methodId)
{
)");

                for (auto& method : service->methods)
                {
                    printer.Print(R"(    case id$name$:
    {
)",
                        "name", method.name);
                    if (method.parameter)
                        printer.Print("        $argument$ argument(parser);\n", "argument", method.parameter->qualifiedName);
                    printer.Print(R"(        if (!parser.FormatFailed())
        {
            tracer.Continue() << "$servicename$.$name$(";
)",
                        "servicename", service->name, "name", method.name);

                    if (method.parameter)
                        for (auto& field : method.parameter->fields)
                        {
                            if (&field != &method.parameter->fields.front())
                                printer.Print(R"(            tracer.Continue() << ", ";
)");
                            printer.Print("            services::PrintField(argument.$field$, tracer);\n", "field", field->name);
                        }

                    printer.Print(R"_(            tracer.Continue() << ")";
        }
        else
            tracer.Continue() << "$servicename$.$name$(parse error)";
        break;
    }
)_",
                        "servicename", service->name, "name", method.name);
                }

                printer.Print(R"(    default:
        tracer.Continue() << "$servicename$ method " << methodId << " not found";
)",
                    "servicename", service->name);

                printer.Print("}\n");
            }
            else
                printer.Print(R"(tracer.Continue() << "$servicename$ method " << methodId << " not found";\ncontents.SkipEverything();\n)", "servicename", service->name);
        }

        return result.str();
    }

    EchoGenerator::EchoGenerator(google::protobuf::compiler::GeneratorContext* generatorContext, const std::string& name, const std::string& suffix, const google::protobuf::FileDescriptor* file, const Options &options)
        : stream(generatorContext->Open(name + suffix))
        , printer(stream.get(), GetPrinterOptions())
        , formatter(true)
        , file(file)
    {
        auto includesByHeader = std::make_shared<IncludesByHeader>();
        includesByHeader->PathSystem("cstdint");
        // includesByHeader->PathSystem("functional");
        //includesByHeader->PathSystem("memory");
        includesByHeader->Path("lib/error.h");
        includesByHeader->Path("lib/inline_string.h");
        includesByHeader->Path("lib/io/io.h");
        // includesByHeader->Path("lib/str.h");

        auto includesBySource = std::make_shared<IncludesBySource>();

        if (options.generate_server) {
            includesByHeader->Path("serialrpc/server.h");
            includesBySource->Path(name + "_server.h");
        }
        if (options.generate_client) {
            includesByHeader->Path("serialrpc/client.h");
            includesBySource->Path(name + "_client.h");
        }
        if (options.generate_shared) {
            includesByHeader->Path("serialrpc/encoding.h");
            includesBySource->Path(name + "_msg.h");
        }
        if (options.generate_server || options.generate_client) {
            includesByHeader->Path(name + "_msg.h");
        }
        // includesByHeader->Path("infra/util/BoundedString.hpp");
        // includesByHeader->Path("infra/util/BoundedVector.hpp");
        // includesByHeader->Path("infra/util/VariadicTemplates.hpp");
        // includesByHeader->Path("protobuf/echo/Echo.hpp");
        // includesByHeader->Path("infra/syntax/ProtoFormatter.hpp");
        // includesByHeader->Path("infra/syntax/ProtoParser.hpp");
        
        EchoRoot root(*file);

        // for (auto& dependency : root.GetFile(*file)->dependencies)
        //     includesByHeader->Path("generated/echo/" + dependency->name + ".pb.hpp");

        formatter.Add(includesByHeader);
        
        // includesBySource->Path("generated/echo/" + root.GetFile(*file)->name + ".pb.hpp");
        
        includesBySource->Path("serialrpc/encoding.h");
        formatter.Add(includesBySource);

        formatter.Add(std::make_shared<SourceSnippet>("using namespace lib"));

        Entities* currentEntity = &formatter;
        for (auto& package : root.GetFile(*file)->packageParts) {
            std::string ns = package;

            auto newNamespace = std::make_shared<Namespace>(ns);
            auto newEntity = newNamespace.get();
            currentEntity->Add(newNamespace);
            currentEntity = newEntity;
        }

        auto decl = std::make_shared<StructForwardDeclaration>("RPCServer");
        currentEntity->Add(decl);

        if (options.generate_shared) {
            for (auto& enum_ : root.GetFile(*file)->enums)
            {
                enumGenerators.emplace_back(std::make_shared<EnumGenerator>(enum_));
                enumGenerators.back()->Run(*currentEntity);
            }

            for (auto& message : root.GetFile(*file)->messages)
            {
                // MessageEnumGenerator messageEnumGenerator(message);
                // messageEnumGenerator.Run(*currentEntity);
                // messageReferenceGenerators.emplace_back(std::make_shared<MessageReferenceGenerator>(message, ""));
                // messageReferenceGenerators.back()->Run(*currentEntity);
                messageGenerators.emplace_back(std::make_shared<MessageGenerator>(message, ""));
                messageGenerators.back()->Run(*currentEntity);
            }

            for (auto& service : root.GetFile(*file)->services) {
                serviceGenerators.emplace_back(std::make_shared<ServiceGenerator>(service, *currentEntity));
            }
        }

        std::vector<EchoService> services;

        auto sorted_services = root.GetFile(*file)->services;
        std::sort(sorted_services.begin(), sorted_services.end(), [](auto &&s1, auto &&s2) {
            return s1->serviceId < s2->serviceId;
        });

        uint32 id = 1;
        for (auto service_ptr : sorted_services) {
            EchoService service = *service_ptr;

            auto sorted_methods = service.methods;
            std::sort(sorted_methods.begin(), sorted_methods.end(), [](auto &&m1, auto &&m2) {
                return m1.methodId < m2.methodId;
            });
            service.methods = sorted_methods;

            for (auto &&method : service.methods) {
                method.methodId = id++;
            }

            services.push_back(service);
        }

        if (options.generate_server) {
            generate_server(services, *currentEntity);
        }
        if (options.generate_client) {
            generate_client(services, *currentEntity);
        }
    }

    google::protobuf::io::Printer::Options EchoGenerator::GetPrinterOptions() const {
        google::protobuf::io::Printer::Options options('$', nullptr);

        options.spaces_per_indent = 4;
        return options;
    }

    void EchoGenerator::GenerateHeader()
    {
        GenerateTopHeaderGuard();
        formatter.PrintHeader(printer);
        GenerateBottomHeaderGuard();
    }

    void EchoGenerator::GenerateSource()
    {
        printer.Print(R"(// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: $filename$

)",
            "filename", file->name());

        formatter.PrintSource(printer, "");
    }

    void EchoGenerator::GenerateTopHeaderGuard()
    {
        std::string filename_identifier = FilenameIdentifier(std::string(file->name()));

//         printer.Print(R"(// Generated by the protocol buffer compiler.  DO NOT EDIT!
// // source: $filename$

// #ifndef echo_$filename_identifier$
// #define echo_$filename_identifier$

// )",
        printer.Print(R"(// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: $filename$

#pragma once

    )",
            "filename", file->name(), "filename_identifier", filename_identifier);
    }

    void EchoGenerator::GenerateBottomHeaderGuard()
    {
        // printer.Print("\n#endif\n");
    }

    TracingEchoGenerator::TracingEchoGenerator(google::protobuf::compiler::GeneratorContext* generatorContext, const std::string& name, const google::protobuf::FileDescriptor* file)
        : stream(generatorContext->Open(name))
        , printer(stream.get(), '$', nullptr)
        , formatter(true)
        , file(file)
    {
        EchoRoot root(*file);

        auto includesByHeader = std::make_shared<IncludesByHeader>();
        includesByHeader->Path("generated/echo/" + root.GetFile(*file)->name + ".pb.hpp");
        includesByHeader->Path("protobuf/echo/TracingEcho.hpp");
        formatter.Add(includesByHeader);

        auto includesBySource = std::make_shared<IncludesBySource>();
        includesBySource->Path("generated/echo/Tracing" + root.GetFile(*file)->name + ".pb.hpp");
        formatter.Add(includesBySource);

        Entities* currentEntity = &formatter;
        for (auto& package : root.GetFile(*file)->packageParts)
        {
            auto newNamespace = std::make_shared<Namespace>(package);
            auto newEntity = newNamespace.get();
            currentEntity->Add(newNamespace);
            currentEntity = newEntity;
        }

        for (auto& service : root.GetFile(*file)->services)
            serviceGenerators.emplace_back(std::make_shared<TracingServiceGenerator>(service, *currentEntity));
    }

    void TracingEchoGenerator::GenerateHeader()
    {
        GenerateTopHeaderGuard();
        formatter.PrintHeader(printer);
        GenerateBottomHeaderGuard();
    }

    void TracingEchoGenerator::GenerateSource()
    {
        printer.Print(R"(// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: $filename$

)",
            "filename", file->name());

        formatter.PrintSource(printer, "");
    }

    void TracingEchoGenerator::GenerateTopHeaderGuard()
    {
        std::string filename_identifier = FilenameIdentifier(std::string(file->name()));

        printer.Print(R"(// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: $filename$

#ifndef echo_tracing_$filename_identifier$
#define echo_tracing_$filename_identifier$

)",
            "filename", file->name(), "filename_identifier", filename_identifier);
    }

    void TracingEchoGenerator::GenerateBottomHeaderGuard()
    {
        printer.Print("\n#endif\n");
    }
    
    void generate_server(std::vector<EchoService> const &services, Entities &formatter) {
        auto server = std::make_shared<Struct>("RPCServer");
        server->Parent("serialrpc::ServerBase");
        auto constructor = std::make_shared<Constructor>("RPCServer", "", Constructor::cExplicit);

        server->Add(constructor);


        size event_id = 1;
        for (auto &&service : services) {
            for (auto &&method : service.methods) {
                if (!method.server_streaming) {
                    continue;
                }

                std::ostringstream result;
                {
                    google::protobuf::io::OstreamOutputStream stream(&result);
                    google::protobuf::io::Printer::Options options('$', nullptr);
                    options.spaces_per_indent = 4;
                    google::protobuf::io::Printer printer(&stream, options);

                    printer.Print("this->send_event($id$, msg);\n", "id", SimpleItoa(event_id++));
                }

                auto func = std::make_shared<Function>("send_" + service.name + "_" + method.name, result.str(), "void", 0);
                func->Parameter(method.result->name + " const &msg");
                server->Add(func);
            }
        }

        size id = 1;

        std::ostringstream result;
        {
            google::protobuf::io::OstreamOutputStream stream(&result);
            google::protobuf::io::Printer::Options options('$', nullptr);
            options.spaces_per_indent = 4;
            google::protobuf::io::Printer printer(&stream, options);
            
            printer.Print("switch (rpc_id) {\n");

            for (auto &&service : services) {
                constructor->Parameter(service.name + " &" + CamelCaseToUnderscores(service.name));
                constructor->Initializer(CamelCaseToUnderscores(service.name) + "(" +  CamelCaseToUnderscores(service.name) + ")");
    
                auto member = std::make_shared<DataMember>(CamelCaseToUnderscores(service.name), service.name + "&");
                server->Add(member);

                for (auto && method : service.methods) {
                    printer.Print("case $num$: {\n", "num", SimpleItoa(id++));

                    printer.Indent();
                    
                    if (method.server_streaming) {
                        printer.Print("bool enabled = conn.read_byte(err);\n");
                        printer.Print("if (err) {\n    return;\n}\n");
                        printer.Print("if (enabled) {\n");
                        printer.Indent();

                        if (method.parameter) {
                            printer.Print("$T$ msg = serialrpc::unmarshal<$T$>(conn, err);\n", "T", method.parameter->name);
                        } else {
                            printer.Print("serialrpc::skip(conn, err);\n");
                        }
                        
                        printer.Print("if (err) {\n    return;\n}\n");
                        printer.Print("RPCServer::ServerErrorHandler handler_err(*this, err);\n");
                        if (method.parameter) {
                            printer.Print("$service$.subscribe_$method$(*this, msg, handler_err);\n", 
                                "service", CamelCaseToUnderscores(service.name), "method", method.name);
                        } else {
                            printer.Print("$service$.subscribe_$method$(*this, handler_err);\n", 
                                "service", CamelCaseToUnderscores(service.name), "method", method.name);
                        }
                        printer.Print("if (err || handler_err) {\n    return;\n}\n");
                        printer.Outdent();

                        printer.Print("} else {\n");
                        
                        printer.Indent();

                        printer.Print("$service$.unsubscribe_$method$();\n", 
                            "service", CamelCaseToUnderscores(service.name), "method", method.name);

                        printer.Outdent();

                        printer.Print("}\n");
                        printer.Print("send_code(conn, serialrpc::Reply, err);\n");
                        printer.Print("if (err) {\n    return;\n}\n");
                    } else {
                        if (method.parameter) {
                            printer.Print("$T$ msg = serialrpc::unmarshal<$T$>(conn, err);\n", "T", method.parameter->name);
                        } else {
                            printer.Print("serialrpc::skip(conn, err);\n");
                        }
                        printer.Print("if (err) {\n    return;\n}\n");

                        if (method.result) {
                            printer.Print("RPCServer::ServerErrorHandler handler_err(*this, err);\n");
                            if (method.parameter) {
                                printer.Print("$T$ resp = $service$.$method$(msg, handler_err);\n", "T", method.result->name, "service", CamelCaseToUnderscores(service.name), "method", method.name);
                            } else {
                                printer.Print("$T$ resp = $service$.$method$(handler_err);\n", "T", method.result->name, "service", CamelCaseToUnderscores(service.name), "method", method.name);
                            }
                            printer.Print("if (err || handler_err) {\n    return;\n}\n");
                            printer.Print("send_reply_msg(conn, resp, err);\n");
                        } else {
                            if (method.parameter) {
                                printer.Print("$service$.$method$(msg, RPCServer::ServerErrorHandler(*this, err));\n", "service", CamelCaseToUnderscores(service.name), "method", method.name);
                            } else {
                                printer.Print("$service$.$method$(RPCServer::ServerErrorHandler(*this, err));\n", "service", CamelCaseToUnderscores(service.name), "method", method.name);
                            }
                            printer.Print("if (err) {\n    return;\n}\n");
                            printer.Print("send_reply_void(conn, err);\n");
                        }
                        
                        printer.Print("if (err) {\n    return;\n}\n");
                    }

                    printer.Print("break;\n");
                    printer.Outdent();
                    printer.Print("}\n");
                }
            }


            printer.Print("default:\n");
            printer.Indent();
            printer.Print("send_code(conn, serialrpc::Unknown, err);\n");
            printer.Outdent();
            printer.Print("} // switch\n");
            
        }

        auto handle_request = std::make_shared<Function>("handle_request", result.str(), "void", Function::fOverride);
        handle_request->Parameter("lib::uint32 rpc_id");
        handle_request->Parameter("lib::io::ReaderWriter &conn");
        handle_request->Parameter("lib::error err");;

        server->Add(handle_request);

        std::ostringstream unsubscribe_all_code;
        {
            google::protobuf::io::OstreamOutputStream stream(&unsubscribe_all_code);
            google::protobuf::io::Printer::Options options('$', nullptr);
            options.spaces_per_indent = 4;
            google::protobuf::io::Printer printer(&stream, options);

            for (auto &&service : services) {
                for (auto method : service.methods) {
                    if (!method.server_streaming) {
                        continue;
                    }
                    printer.Print("this->$service$.unsubscribe_$event$();\n", "service", CamelCaseToUnderscores(service.name), "event", method.name);
                }
            }
        }
        auto unsubscribe_all = std::make_shared<Function>("unsubscribe_all", unsubscribe_all_code.str(), "void", Function::fOverride);
        server->Add(unsubscribe_all);


        formatter.Add(server);
    }

    static std::shared_ptr<Function> generate_push_func(std::vector<EchoService> const &services) {
        std::ostringstream handle_request_code;
        {
            google::protobuf::io::OstreamOutputStream stream(&handle_request_code);
            google::protobuf::io::Printer::Options options('$', nullptr);
            options.spaces_per_indent = 4;
            google::protobuf::io::Printer printer(&stream, options);

            uint32 id = 1;

            printer.Print("switch (event_id) {\n");

            for (auto service : services) {
                for (auto method : service.methods) {
                    if (!method.server_streaming) {
                        continue;
                    }
                    printer.Print("case $num$: {\n", "num", SimpleItoa(id++));
                    printer.Indent();
                    printer.Print("$T$ msg = serialrpc::unmarshal<$T$>(*this->conn, err);\n", "T", method.result->name);
                    printer.Print("if (err) {\n    return;\n}\n");
                    printer.Print("this->$service$.handle_$name$(msg);\n", "service", CamelCaseToUnderscores(service.name), "name", method.name);
                    printer.Print("break;\n");
                    printer.Outdent();
                    printer.Print("}\n");
                }
            }

            printer.Print("default:\n");
            printer.Indent();
            printer.Print("err(\"unknown event\");\n");
            printer.Outdent();
            printer.Print("} // switch\n");

        }
        auto func = std::make_shared<Function>("handle_event", handle_request_code.str(), "void", Function::fOverride);
        func->Parameter("lib::uint32 event_id");
        func->Parameter("lib::error err");
        return func;
    }

    void generate_client(std::vector<EchoService> const &services, Entities &formatter) {
        auto client = std::make_shared<Struct>("RPCClient");
        client->Parent("serialrpc::ClientBase");
        
        // auto conn_member = std::make_shared<DataMember>("conn", "std::shared_ptr<lib::io::ReaderWriter>");
        // client->Add(conn_member);

        auto constructor1 = std::make_shared<Constructor>("RPCClient", "", Constructor::cExplicit);
        constructor1->Initializer("ClientBase()");
        client->Add(constructor1);

        auto constructor2 = std::make_shared<Constructor>("RPCClient", "", Constructor::cExplicit);
        constructor2->Parameter("std::shared_ptr<lib::io::ReaderWriter> const &conn");
        constructor2->Initializer("ClientBase(conn)");
        client->Add(constructor2);

        

        size id = 0;
        

        for (auto &&service : services) {
            auto service_struct = std::make_shared<Struct>(service.name + "Stub");
            // service_struct->Parent(service->name);

            auto service_name = std::make_shared<DataMember>("ServiceName[]", "static constexpr char", "\"" + service.name + "\"");
            service_struct->Add(service_name);
            
            auto conn_field = std::make_shared<DataMember>("client", "RPCClient&");
            service_struct->Add(conn_field);

            service_struct->Add(std::make_shared<DataMember>("mtx", "lib::sync::Mutex"));

            auto sorted_methods = service.methods;
            std::sort(sorted_methods.begin(), sorted_methods.end(), [](auto &&m1, auto &&m2) {
                return m1.methodId < m2.methodId;
            });
            for (auto &&method : sorted_methods) {
                if (!method.server_streaming) {
                    continue;
                }
                auto field = std::make_shared<DataMember>(method.name + "_cb", "std::function<void(" + method.result->name +" const&)>");
                service_struct->Add(field);

            }

            auto constructor = std::make_shared<Constructor>(service.name + "Stub", "", Constructor::cExplicit);
            constructor->Parameter("RPCClient &client");
            constructor->Initializer("client(client)");
            service_struct->Add(constructor);
            service_struct->Field(CamelCaseToUnderscores(service.name));
            
            constructor1->Initializer(CamelCaseToUnderscores(service.name) + "(*this)");
            constructor2->Initializer(CamelCaseToUnderscores(service.name) + "(*this)");


            
            for (auto && method : sorted_methods) {
                id++;

                if (method.server_streaming) {
                    std::ostringstream subscribe_code;
                    {
                        google::protobuf::io::OstreamOutputStream stream(&subscribe_code);
                        google::protobuf::io::Printer::Options options('$', nullptr);
                        options.spaces_per_indent = 4;
                        google::protobuf::io::Printer printer(&stream, options);
    
                        
                        printer.Print("{\n");
                        printer.Indent();
                        printer.Print("sync::Lock lock(this->mtx);\n");
                        printer.Print("this->$name$_cb = cb;\n", "name", method.name);
                        printer.Outdent();
                        printer.Print("}\n");

                        if (method.parameter) {
                            printer.Print("this->client.subscribe($id$, ServiceName, \"$procedure_name$\", req, err);\n", "id", 
                                SimpleItoa(method.methodId),
                                "procedure_name", method.name);
                        } else {
                            printer.Print("this->client.subscribe($id$, ServiceName, \"$procedure_name$\", err);\n", 
                                "id", SimpleItoa(method.methodId),
                                "procedure_name", method.name);
                        }
                    }
                    auto serviceMethod = std::make_shared<Function>("subscribe_" + method.name, subscribe_code.str(), "void", 0);
                    if (method.parameter) {
                        serviceMethod->Parameter(method.parameter->name + " const &req");   
                    }
                    serviceMethod->Parameter("std::function<void(" + method.result->name +" const&)> const &cb");
                    serviceMethod->Parameter("lib::error err");
                    service_struct->Add(serviceMethod);

                    std::ostringstream unsubscribe_code;
                    {
                        google::protobuf::io::OstreamOutputStream stream(&unsubscribe_code);
                        google::protobuf::io::Printer::Options options('$', nullptr);
                        options.spaces_per_indent = 4;
                        google::protobuf::io::Printer printer(&stream, options);
    
                        
                        printer.Print("sync::Lock lock(this->mtx);\n");
                        printer.Print("this->client.unsubscribe($id$, ServiceName, \"$procedure_name$\", err);\n", 
                                "id", SimpleItoa(method.methodId),
                                "procedure_name", method.name);
                        printer.Print("if (err) {\n    return;\n}\n");
                        printer.Print("this->$name$_cb = {};\n", "name", method.name);
                        
                    }
                    serviceMethod = std::make_shared<Function>("unsubscribe_" + method.name, unsubscribe_code.str(), "void", 0);
                    serviceMethod->Parameter("lib::error err");
                    service_struct->Add(serviceMethod);

                    std::ostringstream handle_code;
                    {
                        google::protobuf::io::OstreamOutputStream stream(&handle_code);
                        google::protobuf::io::Printer::Options options('$', nullptr);
                        options.spaces_per_indent = 4;
                        google::protobuf::io::Printer printer(&stream, options);
    
                        
                        printer.Print("sync::Lock lock(this->mtx);\n");
                        printer.Print("if (this->$name$_cb) {\n    this->$name$_cb(msg);\n}\n", "name", method.name);
                        
                    }
                    serviceMethod = std::make_shared<Function>("handle_" + method.name, handle_code.str(), "void", 0);
                    serviceMethod->Parameter(method.result->name + " const &msg");
                    service_struct->Add(serviceMethod);

                } else {
                    std::string rettype;
                    if (method.result) {
                        rettype = method.result->name;
                    } else {
                        rettype = "void";
                    }

                    std::ostringstream code;
                    {
                        google::protobuf::io::OstreamOutputStream stream(&code);
                        google::protobuf::io::Printer::Options options('$', nullptr);
                        options.spaces_per_indent = 4;
                        google::protobuf::io::Printer printer(&stream, options);
    
                        if (method.result) {
                            if (method.parameter) {
                                printer.Print("return this->client.call<$Req$, $Ret$>($id$, ServiceName, \"$procedure_name$\", req, err);\n", 
                                    "id", SimpleItoa(id),
                                    "procedure_name", method.name,
                                    "Req", method.parameter->name + " const&", 
                                    "Ret", rettype);
                            } else {
                                printer.Print("return this->client.call<$Ret$>($id$, ServiceName, \"$procedure_name$\", err);\n", 
                                    "id", SimpleItoa(id),
                                    "procedure_name", method.name,
                                    "Ret", rettype);
                            }
                        } else {
                            if (method.parameter) {
                                printer.Print("this->client.call_void<$Req$>($id$, ServiceName, \"$procedure_name$\", req, err);\n", 
                                    "id", SimpleItoa(id),
                                    "procedure_name", method.name,
                                    "Req", method.parameter->name + " const&");
                            } else {
                                printer.Print("this->client.call_void($id$, ServiceName, \"$procedure_name$\", err);\n", 
                                    "id", SimpleItoa(id),
                                    "procedure_name", method.name);
                            }
                        }
                        
                    }
                    auto serviceMethod = std::make_shared<Function>(method.name, code.str(), rettype, 0);
                    
                    if (method.parameter) {
                            serviceMethod->Parameter(method.parameter->name + " const &req");
                    }
                    serviceMethod->Parameter("lib::error err");
                    service_struct->Add(serviceMethod);
                }
            }

            

            client->Add(service_struct);
        }

        client->Add(generate_push_func(services));

        // client->Add(func);
        formatter.Add(client);
    }
} // namespace application
