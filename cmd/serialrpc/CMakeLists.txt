add_executable(serialrpc
  call.cc
  main.cc
)

target_compile_features(serialrpc PRIVATE cxx_std_23)
target_link_libraries(serialrpc PRIVATE
  serialrpc::client
  protobuf::libprotobuf
)
