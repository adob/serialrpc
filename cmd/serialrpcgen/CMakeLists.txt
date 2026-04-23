
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

add_executable(serialrpcgen
  CppFormatter.cc
  EchoObjects.cc
  ProtoCEchoPlugin.cc
  serialrpc.pb.cc
  serialrpcgen.cc
)

target_compile_features(serialrpcgen PRIVATE cxx_std_23)

target_include_directories(serialrpcgen PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${_serialrpc_parent_dir}
)

target_link_libraries(serialrpcgen PRIVATE
  baselib::baselib
  protobuf::libprotobuf
  protobuf::libprotoc
)