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
}]
