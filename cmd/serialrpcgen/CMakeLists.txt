
if(NOT DEFINED _serialrpc_root_dir)
  get_filename_component(_serialrpc_root_dir
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    REALPATH
  )
endif()


get_filename_component(_serialrpc_parent_dir
  "${_serialrpc_root_dir}/.."
  REALPATH
)

file(MAKE_DIRECTORY ${_serialrpc_root_dir}/generated)

if(TARGET protobuf::protoc)
  set(_protoc_exe $<TARGET_FILE:protobuf::protoc>)
else()
  find_program(_protoc_exe NAMES protoc REQUIRED)
endif()

add_custom_command(
  OUTPUT
    ${_serialrpc_root_dir}/generated/serialrpc.pb.cc
    ${_serialrpc_root_dir}/generated/serialrpc.pb.h
  COMMAND ${_protoc_exe}
    --cpp_out=${_serialrpc_root_dir}/generated
    -I ${_serialrpc_root_dir}
    -I ${SERIALRPC_PROTOBUF_PROTO_DIR}
    ${_serialrpc_root_dir}/serialrpc.proto
  DEPENDS ${_serialrpc_root_dir}/serialrpc.proto
  COMMENT "Generating serialrpc.pb.cc/h"
)

add_executable(serialrpcgen_plugin
  cpp_formatter.cc
  serialrpc_objects.cc
  protoc_serialrpc_plugin.cc
  ${_serialrpc_root_dir}/generated/serialrpc.pb.cc
  serialrpcgen_plugin.cc
)

target_compile_features(serialrpcgen_plugin PRIVATE cxx_std_23)

target_include_directories(serialrpcgen_plugin PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${_serialrpc_parent_dir}
)

target_link_libraries(serialrpcgen_plugin PRIVATE
  baselib::baselib
  protobuf::libprotobuf
  protobuf::libprotoc
)
