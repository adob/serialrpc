GENERATED = [{
    "inputs": [
        "../serialrpc.proto",
        "generate_serialrpcpb.py",
    ],
    "outputs": [
        "serialrpc.pb.h",
        "serialrpc.pb.cc",
        "serialrpcpb.cc",
    ],
    "tools": ["protoc"],
    "command": [
        "python3",
        "generate_serialrpcpb.py",
        "{outdir}",
    ],
}, {
    "inputs": [
        "../serialrpc.proto",
        "../serialrpc_protocol.proto",
        "../cmd/serialrpcgen/BUILD.py",
        "../cmd/serialrpcgen/cpp_formatter.h",
        "../cmd/serialrpcgen/protoc_serialrpc_plugin.h",
        "../cmd/serialrpcgen/serialrpc_objects.h",
    ],
    "outputs": [
        "serialrpc_protocol/client.cc",
        "serialrpc_protocol/server.cc",
        "serialrpc_protocol/msg.cc",
    ],
    "tools": ["protoc"],
    "command": [
        "protoc",
        "--plugin=protoc-gen-serialrpc={root}/build/release/bin/serialrpcgen",
        "--serialrpc_out=module=serialrpc.generated.serialrpc_protocol:{outdir}",
        "-I",
        "{root}/third_party/serialrpc",
        "{root}/third_party/serialrpc/serialrpc_protocol.proto",
    ],
}]
