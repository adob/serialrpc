
CPMFindPackage(
  NAME protobuf
  VERSION 32.1.0
  GITHUB_REPOSITORY protocolbuffers/protobuf
  GIT_TAG v32.1
)

get_filename_component(_serialrpc_parent_dir
  "${_serialrpc_root_dir}/.."
  REALPATH
)

add_executable(serialrpcgen_plugin
  CppFormatter.cc
  EchoObjects.cc
  ProtoCEchoPlugin.cc
  serialrpc.pb.cc
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