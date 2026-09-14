buildtool_add_executable(serialrpc
  LIBRARY serialrpc_sources
  SOURCES main.cc call.cc
  PRIVATE_LIBRARIES protobuf::libprotobuf
  DEPENDS serialrpc_generate_protocol)
