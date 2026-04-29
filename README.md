# serialrpc

`serialrpc` is a small C++23 RPC runtime and code generator for typed
request/reply calls and server-pushed events over an ordered byte stream.

The library is intended for host-to-device links such as USB CDC ACM.
A `.proto` file defines messages and services;
the `serialrpcgen` protoc plugin generates compact message structs plus client
stubs and server base classes.

## What is Included

- `serialrpc` static library with client, server, RPC, and encoding support
- `cmd/serialrpcgen`, a protoc plugin that generates `*.pb_msg.*`,
  `*.pb_client.*`, and `*.pb_server.*`
- `serialrpc.proto`, which defines the custom options used by service and
  message definitions
- `serialrpc_protocol.proto`, the generated service-description payload sent
  during the connection handshake
- `example.proto` and checked-in generated example files under `generated/`
- unit and end-to-end tests for encoding, generated bindings, calls, and events

## Protocol Model

There are two connection roles:

- **Client**: owns a `serialrpc::Client`, connects generated stubs, sends
  requests, waits for replies, and dispatches server events to callbacks.
- **Server**: owns a `serialrpc::Server<...>` built from generated service base
  implementations, accepts a `serial::Conn`, dispatches requests, and sends
  replies or events.

Services are identified by UUID rather than by service name on the wire. The
server sends each service UUID, major version, minor version, and endpoint count
during the hello message. The client matches that metadata against the generated
stubs passed to `serialrpc::connect`.

Example service definition:

```proto
syntax = "proto3";

package example;

import "serialrpc.proto";

message SumRequest {
  int32 left = 1;
  int32 right = 2;
}

message SumResponse {
  int32 answer = 1;
}

message SumEventsRequest {
  int32 v = 1;
}

message SumEvent {
  int32 event = 1;
}

service SumService {
  option (uuid) = "4f90fb19-7b58-4755-bb1f-3c81dc0a6d4d";

  rpc sum(SumRequest) returns (SumResponse) {
    option (method_id) = 1;
  }

  rpc sum_events(SumEventsRequest) returns (stream SumEvent) {
    option (method_id) = 2;
  }
}
```

The generator requires a UUID on each service and a `method_id` option on each
method. `major_version` and `minor_version` service options are available and
default to `0`.

## Generated C++ Shape

For a proto package named `example`, generated C++ is placed in namespace
`examplepb`.

Message generation produces plain structs with default-initialized fields,
field-number constants, equality operators, and static `marshal` / `unmarshal`
functions:

```cpp
examplepb::SumRequest req{.left = 10, .right = 20};
```

Client generation produces one stub per service. Pass the stubs to
`serialrpc::connect`, then call RPC methods directly:

```cpp
examplepb::SumServiceStub sum_stub;
auto client = serialrpc::connect(conn, {&sum_stub}, err);

examplepb::SumResponse resp =
    sum_stub.sum(examplepb::SumRequest{.left = 10, .right = 20}, err);

sum_stub.subscribe_sum_events(
    examplepb::SumEventsRequest{.v = 42},
    [](examplepb::SumEvent const &event) {
        // handle event
    },
    err);
```

Server generation produces abstract service base classes. Implement the virtual
methods, construct `serialrpc::Server` with those services, and call `accept`
or `serve`:

```cpp
struct Summer : examplepb::SumServiceBase {
    std::function<void(examplepb::SumEvent const &)> cb;

    examplepb::SumResponse sum(examplepb::SumRequest const &req,
                               lib::error) override {
        return {.answer = req.left + req.right};
    }

    void subscribe_sum_events(
        examplepb::SumEventsRequest const &req,
        std::function<void(examplepb::SumEvent const &)> const &callback,
        lib::error) override {
        cb = callback;
    }

    void unsubscribe_sum_events(lib::error) override {
        cb = nullptr;
    }
};

Summer summer;
serialrpc::Server server(summer);
server.accept(conn, err);
```

## Wire Protocol

`serialrpc` runs over an ordered bidirectional byte stream. The transport moves
bytes; the runtime defines the hello exchange, request IDs, server message
types, and message encoding.

### Handshake

A client starts by sending a one-byte hello:

```text
client -> server: 0xF1  ClientHello
```

The server replies with `ServerHello`, followed by an encoded
`serialrpcpb::ServerHello` payload and a top-level `End` tag:

```text
server -> client: 0xF9  ServerHello
                  encoded ServerHello {
                    protocol_version = 1
                    repeated services = {
                      uuid
                      major_version
                      minor_version
                      num_endpoints
                    }
                  }
```

The client validates the protocol version and matches services by UUID and
version. It assigns contiguous runtime endpoint IDs from the advertised service
order and endpoint counts.

To close a running session, the client writes request ID `0` as a varuint32.
The server responds with:

```text
server -> client: 0xFA  ServerGoodbye
```

### Client Requests

Every normal request starts with a varuint32 endpoint ID followed by an encoded
request message:

```text
varuint32 endpoint_id
encoded request message
```

Endpoint IDs sent by the client are one-based. The server subtracts one before
indexing its dispatch table. Generated client stubs calculate endpoint IDs from
the service offset learned during the hello exchange.

For methods with a `Nothing` request, the request body is just an `End` tag.

Streaming-response methods are represented as subscriptions. The generated
client writes the endpoint ID, a one-byte enable flag, and then the optional
subscription request:

```text
varuint32 endpoint_id
uint8 enabled       # 1 to subscribe, 0 to unsubscribe
encoded request     # present for subscribe requests
```

Subscription setup and teardown receive a normal `Reply` acknowledgement.

### Server Messages

Every server-to-client message starts with a one-byte message type:

```text
0xF0  Reply
0xF1  ErrorReply
0xF2  Event
0xF3  Log
0xF4  Unknown
0xF5  TooBig
0xF6  BadMessage
0xF7  FatalError
0xF9  ServerHello
0xFA  ServerGoodbye
```

A successful response with a return value is:

```text
0xF0
encoded response message
```

A successful response for `Nothing` is just:

```text
0xF0
```

An event is:

```text
0xF2
varuint32 event_id
encoded event message
```

Generated server event helpers write `event_id + 1`, matching the client-side
one-based endpoint IDs. Events whose response type is `Nothing` contain no
message body after the event ID.

An error reply is:

```text
0xF1
chunked UTF-8 error text
```

`Log` messages carry a varuint32 byte count followed by that many log bytes.

## Message Encoding

Generated encoders write only fields whose value is not the default value.
Numeric zero, `false`, and empty byte/string fields are omitted.

Each encoded field starts with a varuint32 tag:

```text
tag = (field_number << 3) | wire_type
```

The low three bits are the wire type:

```text
0  VarInt
1  I64
2  Len
3  Start
4  End
5  I32
```

Supported field payloads:

```text
int32/int64      signed varint
uint32/uint64    unsigned varint
bool             presence-only field; true is encoded, false is omitted
float32          4 little-endian IEEE-754 bytes
float64          8 little-endian IEEE-754 bytes
bytes/string     varuint32 length followed by raw bytes
message          Start tag, nested fields, End tag
```

Top-level messages also end with an `End` tag. Unknown fields can be skipped
from their wire type, so newer senders can add fields that older receivers
ignore.

Bounded byte and string fields use `bytes_size` and `string_size` options and
are generated as fixed-capacity `lib::InlineString<N>` values. The plugin also
defines `array_size` for bounded repeated fields.

## Repository Layout

```text
.
├── client.{h,cc}                 # Client runtime and input worker
├── server.{h,cc}                 # Server runtime and dispatch helpers
├── rpc.{h,cc}                    # Message type enums and RPC errors
├── encoding.{h,cc}               # Compact field/tag encoding
├── internal.{h,cc}               # Shared helpers
├── serialrpc.proto               # serialrpc custom proto options
├── serialrpc_protocol.proto      # ServerHello service-description schema
├── cmd/serialrpcgen/             # protoc plugin implementation
├── generated/                    # Checked-in generated protocol/example code
├── example.proto                 # Example services and messages
├── encoding_test.cc              # Encoding tests
└── e2e_test.cc                   # In-memory client/server integration test
```

## Build and Test

This project is built with CMake and uses CPM. The current local build notes use
a checked-out `baselib` source tree:

```sh
cmake -S . -B build -DBUILD_TESTING=ON # -DCPM_baselib_SOURCE=$HOME/deps/baselib
cmake --build build
cd build && ctest --output-on-failure
```

If you do not provide `DCPM_baselib_SOURCE`, CPM will use the pinned
`adob/baselib` package from `package-lock.cmake`.

