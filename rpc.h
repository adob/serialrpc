#pragma once
#include "lib/base.h"

namespace serialrpc {
    using namespace lib;
    
    enum ServerMessageType : byte {
        Reply         = 1,
        Event         = 2,
        ErrorReply    = 3,
        Unknown       = 4,
        TooBig        = 5,
        BadMessage    = 6,
        FatalError    = 7,
        
        ServerHello   = 8,
        ServerGoodbye = 9,
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
