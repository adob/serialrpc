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
    ],
    "outputs": [
        "serialrpc_protocol/client.cc",
        "serialrpc_protocol/server.cc",
        "serialrpc_protocol/msg.cc",
    ],
    "build_tools": {
        "serialrpcgen": "../cmd/serialrpcgen",
    },
    "command": [
        "protoc",
        "--plugin=protoc-gen-serialrpc={tool:serialrpcgen}",
        "--serialrpc_out=module=serialrpc.generated.serialrpc_protocol:{outdir}",
        "-I",
        "{srcdir}/..",
        "{srcdir}/../serialrpc_protocol.proto",
    ],
}]
