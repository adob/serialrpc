#pragma once
#include "lib/base.h"

namespace serialrpc {
    using namespace lib;
    
    enum ServerMessageType : byte {
        Reply         = 1,
        ErrorReply    = 2,
        UnknownRPC    = 3,
        TooBig        = 4,
        BadMessage    = 5,
        FatalError    = 6,
        
        ServerHello   = 7,
        ServerGoodbye = 8,
    };
    
    enum ClientMessageType : byte {
        Request       = 0xF0,
        ClientHello   = 0xF1,
        ClientGoodbye = 0xF2,
    };
    
    struct ErrReply : ErrorBase<ErrReply, "serialrpc error reply"> {};
    struct ErrUnknownMethod  : ErrorBase<ErrUnknownMethod, "serialrpc unknown method"> {};
    struct ErrRequestTooBig  : ErrorBase<ErrRequestTooBig, "serialrpc request too big"> {};
    struct ErrResponseTooBig : ErrorBase<ErrResponseTooBig, "serialrpc request too big"> {};
    struct ErrBadMessage     : ErrorBase<ErrBadMessage, "serialrpc bad message"> {};
    struct ErrFatal          : ErrorBase<ErrFatal, "serialrpc fatal error"> {};
    struct ErrCorruption     : ErrorBase<ErrCorruption, "serialrpc I/O corruption"> {};
}
