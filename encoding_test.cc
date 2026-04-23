#include "encoding.h"
#include "example.pb_msg.h"

#include "lib/testing/testing.h"
#include "lib/io/io.h"
#include "lib/print.h"


using namespace lib;
using namespace lib::testing;
using namespace serialrpc;

void test_encode_decode(T &t) {
    examplepb::SumRequest req = { .left = 41, .right = 42 };
    
    io::Buffer buffer;
    marshal(buffer, req, error::panic);

    examplepb::SumRequest out = unmarshal<examplepb::SumRequest>(buffer, error::panic);

    if (out.left != 41 || out.right != 42) {
        t.errorf("unmarshal() got left %v, right %v; want left 41, right 42", out.left, out.right);
    }
}

void test_encode_decode_string(T &t) {
    examplepb::Message2 msg = { .data = "hello" };
    
    io::Buffer buffer;
    marshal(buffer, msg, error::panic);
    print "buffer %v % x" % buffer.length(), buffer.str();

    msg = unmarshal<examplepb::Message2>(buffer, error::panic);

    if (msg.data != "hello") {
        t.errorf("unmarshal() got %q, want \"hello\"", str(msg.data));
    }
}

void test_encode_decode2(T &t) {
    examplepb::SumRequest sum = { .left = 41, .right = 42 };
    examplepb::Message2 msg2 = {.sum_request = sum};
    
    io::Buffer buffer;
    marshal(buffer, msg2, error::panic);
    print "buffer %v % x" % buffer.length(), buffer.str();

    examplepb::Message2 out = unmarshal<examplepb::Message2>(buffer, error::panic);

    if (out.sum_request.left != 41 || out.sum_request.right != 42) {
        t.errorf("unmarshal() got left %v, right %v; want left 41, right 42", out.sum_request.left, out.sum_request.right);
    }
}

void test_encode_decode_empty(T &t) {
    examplepb::SumRequest req = { .left = 0, .right = 0 };
    
    io::Buffer buffer;
    marshal(buffer, req, error::panic);

    print "buffer %v % x" % buffer.length(), buffer.str();
    String data = buffer.str();

    examplepb::SumRequest out = unmarshal<examplepb::SumRequest>(buffer, error::panic);

    if (out.left != 0 || out.right != 0) {
        t.errorf("unmarshal() got left %v, right %v; want left 0, right 0", out.left, out.right);
    }

    if (len(data) != 1  ) {
        t.errorf("len(data) = %v; want 1; data = 0x% x", len(data), data);
    }
}

void test_encode_decode_empty_submessage(T &t) {
    examplepb::SumRequest sum = { .left = 0, .right = 0 };
    examplepb::Message2 msg2 = {.sum_request = sum, .data = ""};
    
    io::Buffer buffer;
    marshal(buffer, msg2, error::panic);

    print "buffer %v % x" % buffer.length(), buffer.str();
    String data = buffer.str();

    examplepb::SumRequest out = unmarshal<examplepb::SumRequest>(buffer, error::panic);

    if (out.left != 0 || out.right != 0) {
        t.errorf("unmarshal() got left %v, right %v; want left 0, right 0", out.left, out.right);
    }

    if (len(data) != 1  ) {
        t.errorf("len(data) = %v; want 1; data = 0x% x", len(data), data);
    }
}
