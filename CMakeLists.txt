cmake_minimum_required(VERSION 3.16)

get_filename_component(
  CURRENT_DIRNAME
  ${CMAKE_CURRENT_LIST_DIR}
  NAME
)

project(serialrpc VERSION 0.0.0 LANGUAGES CXX)

include(CTest)

include(cmake/CPM.cmake)

CPMAddPackage(
  NAME baselib
  GITHUB_REPOSITORY adob/baselib
  VERSION 0.0.0
  GIT_TAG 01860788104c73701c88c00238c49bb7f2f740d4
)

if(NOT CMAKE_CROSSCOMPILING)
  CPMFindPackage(
    NAME protobuf
    VERSION 32.1.0
    GITHUB_REPOSITORY protocolbuffers/protobuf
    GIT_TAG v32.1
    OPTIONS
      "protobuf_BUILD_TESTS OFF"
      "protobuf_BUILD_CONFORMANCE OFF"
      "protobuf_BUILD_EXAMPLES OFF"
  )
endif()

CPMFindPackage(
  NAME Boost
  VERSION 1.87.0 # Versions less than 1.85.0 may need patches for installation targets.
  URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
  URL_HASH SHA256=7da75f171837577a52bbf217e17f8ea576c7c246e4594d617bfde7fafd408be5
  FIND_PACKAGE_ARGUMENTS "CONFIG;COMPONENTS;unordered"
  OPTIONS "BOOST_ENABLE_CMAKE ON" "BOOST_SKIP_INSTALL_RULES ON" # Set `OFF` for installation
          "BUILD_SHARED_LIBS OFF"
)

add_library(${PROJECT_NAME}_server STATIC
  encoding.cc
  rpc.cc
  server.cc
  generated/serialrpc_protocol.pb_msg.cc
)

add_library(${PROJECT_NAME}::server ALIAS ${PROJECT_NAME}_server)
target_compile_features(${PROJECT_NAME}_server PUBLIC cxx_std_23)

get_filename_component(_parent_dir
  "${CMAKE_CURRENT_LIST_DIR}/.."
  REALPATH
)

# CPM checks this repo out as serialrpc-src, but public headers use includes
# like "serialrpc/encoding.h". Create a build-tree include prefix that matches.
set(SERIALRPC_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${SERIALRPC_INCLUDE_DIR})
if(NOT EXISTS ${SERIALRPC_INCLUDE_DIR}/serialrpc)
  file(CREATE_LINK
    ${CMAKE_CURRENT_LIST_DIR}
    ${SERIALRPC_INCLUDE_DIR}/serialrpc
    SYMBOLIC
  )
endif()

target_include_directories(${PROJECT_NAME}_server PUBLIC
  $<BUILD_INTERFACE:${SERIALRPC_INCLUDE_DIR}>
  $<BUILD_INTERFACE:${_parent_dir}>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME}_server PUBLIC baselib::baselib)

add_library(${PROJECT_NAME}_client STATIC
  client.cc
  internal.cc
)
add_library(${PROJECT_NAME}::client ALIAS ${PROJECT_NAME}_client)
target_compile_features(${PROJECT_NAME}_client PUBLIC cxx_std_23)
target_include_directories(${PROJECT_NAME}_client PUBLIC
  $<BUILD_INTERFACE:${SERIALRPC_INCLUDE_DIR}>
  $<BUILD_INTERFACE:${_parent_dir}>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME}_client PUBLIC ${PROJECT_NAME}::server)

target_link_libraries(${PROJECT_NAME}_client PUBLIC
  Boost::headers
)

if(NOT CMAKE_CROSSCOMPILING)
  set(SERIALRPC_PROTOBUF_PROTO_DIR ${protobuf_SOURCE_DIR}/src)
  set(_serialrpc_root_dir ${CMAKE_CURRENT_LIST_DIR})
  add_subdirectory(cmd/serialrpcgen)

  if(TARGET serialrpcgen_plugin)
    target_include_directories(serialrpcgen_plugin PRIVATE
      ${SERIALRPC_INCLUDE_DIR}
    )
  endif()

  # Find protoc (protobuf is loaded by cmd/serialrpcgen)
  if(TARGET protobuf::protoc)
    set(_protoc_exe $<TARGET_FILE:protobuf::protoc>)
  else()
    find_program(_protoc_exe NAMES protoc REQUIRED)
  endif()

  # Generate serialrpc_protocol files required by the library
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)
  add_custom_command(
    OUTPUT
      ${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_msg.cc
      ${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_msg.h
      ${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_client.cc
      ${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_client.h
      ${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_server.cc
      ${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_server.h
    COMMAND ${_protoc_exe}
      --plugin=protoc-gen-serialrpc=$<TARGET_FILE:serialrpcgen_plugin>
      --serialrpc_out=${CMAKE_CURRENT_LIST_DIR}/generated
      -I ${CMAKE_CURRENT_LIST_DIR}
      -I ${SERIALRPC_PROTOBUF_PROTO_DIR}
      ${CMAKE_CURRENT_LIST_DIR}/serialrpc_protocol.proto
    DEPENDS
      ${CMAKE_CURRENT_LIST_DIR}/serialrpc_protocol.proto
      serialrpcgen_plugin
    COMMENT "Generating serialrpc_protocol.pb_*.cc/h"
  )
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  add_custom_command(
    OUTPUT
      ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_msg.cc
      ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_msg.h
      ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_client.cc
      ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_client.h
      ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_server.cc
      ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_server.h
    COMMAND ${_protoc_exe}
      --plugin=protoc-gen-serialrpc=$<TARGET_FILE:serialrpcgen_plugin>
      --serialrpc_out=${CMAKE_CURRENT_LIST_DIR}/generated
      -I ${CMAKE_CURRENT_LIST_DIR}
      -I ${SERIALRPC_PROTOBUF_PROTO_DIR}
      ${CMAKE_CURRENT_LIST_DIR}/example.proto
    DEPENDS
      ${CMAKE_CURRENT_LIST_DIR}/example.proto
      serialrpcgen_plugin
    COMMENT "Generating example.pb_*.cc/h"
  )

  add_executable(${PROJECT_NAME}_tests
    ${CMAKE_CURRENT_LIST_DIR}/encoding_test.cc
    ${CMAKE_CURRENT_LIST_DIR}/e2e_test.cc
    ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_msg.cc
    ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_client.cc
    ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_server.cc
  )

  target_compile_features(${PROJECT_NAME}_tests PRIVATE cxx_std_23)
  target_include_directories(${PROJECT_NAME}_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/generated
  )

  target_link_libraries(${PROJECT_NAME}_tests PRIVATE
    ${PROJECT_NAME}::client
    baselib::test_main
  )

  add_test(NAME ${PROJECT_NAME}_tests COMMAND ${PROJECT_NAME}_tests)
  set_tests_properties(${PROJECT_NAME}_tests PROPERTIES TIMEOUT 30)
endif()
