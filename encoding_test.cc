#include "encoding.h"
#include "example.pb.h"
#include "lib/testing/testing.h"
#include "lib/io/io.h"

using namespace lib;
using namespace lib::testing;
using namespace serialrpc;

void test_encode_decode(T &t) {
    SumRequest req = { .left = 41, .right = 42 };
    
    io::Buffer buffer;
    marshal(buffer, req, error::panic);

    SumRequest out = unmarshal<SumRequest>(buffer, error::panic);

    if (out.left != 41 || out.right != 42) {
        t.errorf("unmarhsal() got left %v, right %v; want left 41, right 42", out.left, out.right);
    }
}

void test_encode_decode2(T &t) {
    SumRequest sum = { .left = 41, .right = 42 };
    Message2 msg2 = {.sum_request = sum};
    
    io::Buffer buffer;
    marshal(buffer, msg2, error::panic);

    Message2 out = unmarshal<Message2>(buffer, error::panic);

    if (out.sum_request.left != 41 || out.sum_request.right != 42) {
        t.errorf("unmarhsal() got left %v, right %v; want left 41, right 42", out.sum_request.left, out.sum_request.right);
    }
}