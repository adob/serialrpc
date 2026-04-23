#include "google/protobuf/compiler/plugin.h"

#include "ProtoCEchoPlugin.h"
#include "lib/debug/debug.h"
#include <google/protobuf/compiler/cpp/helpers.h>

using namespace lib;

int main(int argc, char *argv[]) {
    // debug::init();


    // google::protobuf::compiler::cpp::FieldConstantName(nil);

    application::CppInfraCodeGenerator generator;
    return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}