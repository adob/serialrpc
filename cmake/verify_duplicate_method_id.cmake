execute_process(
  COMMAND ${PROTOC}
    --plugin=protoc-gen-serialrpc=${PLUGIN}
    --serialrpc_out=${OUTPUT_DIR}
    -I ${SERIALRPC_DIR}
    -I ${PROTOBUF_PROTO_DIR}
    ${SERIALRPC_DIR}/testdata/duplicate_method_id.proto
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error_output
)

if(result EQUAL 0)
  message(FATAL_ERROR "serialrpcgen accepted duplicate method IDs")
endif()

set(expected "methods first and second use duplicate method_id 1")
if(NOT "${output}${error_output}" MATCHES "${expected}")
  message(FATAL_ERROR
    "serialrpcgen failed without the expected diagnostic:\n${output}${error_output}"
  )
endif()
