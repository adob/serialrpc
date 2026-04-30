#pragma once

#include "lib/error.h"
#include "lib/io/io.h"

namespace serialrpc {
    using namespace lib;
    
    const int ProtocolVersion = 1;

    enum ServerMessageType : byte {
        Reply         = 0xF0,
        ErrorReply    = 0xF1,
        Event         = 0xF2,
        Log           = 0xF3,
        Unknown       = 0xF4,
        TooBig        = 0xF5,
        BadMessage    = 0xF6,
        FatalError    = 0xF7,
        
        ServerHello   = 0xF9,
        ServerGoodbye = 0xFA,
    };
    
    enum ClientMessageType : byte {
        Request       = 0xF0,
        ClientHello   = 0xF1,
        ClientGoodbye = 0xF2,
    };
    
    struct ErrReply : lib::Error {
        str io_name;
        str service_name;
        str procedure_name;
        str msg;
        ErrReply(str io_name, str service_name, str procedure_name, str msg) : 
              io_name(io_name)
            , service_name(service_name)
            , procedure_name(procedure_name)
            , msg(msg)
            {}
        void fmt(io::Writer &out, error err) const override;
    } ;
    
    struct ErrUnknownMethod  : ErrorBase<ErrUnknownMethod, "serialrpc unknown method"> {};
    struct ErrRequestTooBig  : ErrorBase<ErrRequestTooBig, "serialrpc request too big"> {};
    struct ErrResponseTooBig : ErrorBase<ErrResponseTooBig, "serialrpc request too big"> {};
    struct ErrBadMessage     : ErrorBase<ErrBadMessage, "serialrpc bad message"> {};
    struct ErrFatal          : ErrorBase<ErrFatal, "serialrpc fatal error"> {};
    struct ErrClosed         : ErrorBase<ErrClosed, "serialrpc connection closed"> {};
    struct ErrUnsolicitedServerGoodbye : ErrorBase<ErrUnsolicitedServerGoodbye, "unsolicited server goodbye"> {};
    struct ErrCorruption     : ErrorBase<ErrCorruption, "serialrpc I/O corruption"> {};

    struct Err : ErrorBase<Err> {
        io::WriterTo const *details = nil;
        const Error *err = nil;

        Err(io::WriterTo const &details) : details(&details) {}
        Err(io::WriterTo const &details, const Error &err) :  details(&details), err(&err) {}

        void fmt(io::Writer &out, error err) const override;
    } ;
}
