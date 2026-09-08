#include "lib/error.h"
#include "lib/fmt/fmt.h"
#include "lib/os/stdio.h"
#include "serialrpc/client.h"
#include "serialrpc/generated/serialrpc_protocol.pb_client.h"

using namespace lib;

namespace {
    void usage(io::Writer &out) {
        fmt::fprintf(out,
            "Usage: serialrpc <command> <endpoint>\n"
            "\n"
            "Commands:\n"
            "  list, ls    List services exposed by the endpoint\n");
    }

    int list_services(str endpoint, error err) {
        serialrpcpb::DiscoveryServiceStub discovery;
        auto client = serialrpc::connect(endpoint, {&discovery}, err);
        if (err || client == nil) {
            return 1;
        }

        auto response = discovery.ListServices({}, err);
        if (err) {
            client->close(error::ignore);
            return 1;
        }

        for (auto const &service : response.services) {
            fmt::printf("%s\n", str(service.name));
            for (auto const &method : service.methods) {
                fmt::printf("  rpc %s\n", str(method.name));
            }
        }

        client->close(err);
        return err ? 1 : 0;
    }
}

int main(int argc, char *argv[]) {
    ErrorFunc err = [](Error const &e) {
        fmt::fprintf(os::stderr, "serialrpc: %v\n", e);
    };

    str command;
    if (argc >= 2) {
        command = str::from_c_str(argv[1]);
    }

    if (argc == 2 && (command == "-h" || command == "--help")) {
        usage(os::stdout);
        return 0;
    }

    if (argc != 3) {
        usage(os::stderr);
        return 2;
    }

    if (command != "list" && command != "ls") {
        fmt::fprintf(os::stderr, "serialrpc: unknown command %q\n", argv[1]);
        usage(os::stderr);
        return 2;
    }

    return list_services(str::from_c_str(argv[2]), err);
}
