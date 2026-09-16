cmake_minimum_required(VERSION 3.30)

get_filename_component(
  CURRENT_DIRNAME
  ${CMAKE_CURRENT_LIST_DIR}
  NAME
)

project(serialrpc VERSION 0.0.0 LANGUAGES CXX)
# Public imports use the standalone maps published by buildtool.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(CTest)
set(SERIALRPC_BUILD_TESTING_DEFAULT OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(SERIALRPC_BUILD_TESTING_DEFAULT ON)
endif()
# A parent's BUILD_TESTING enables its own tests, not every fetched dependency's.
option(SERIALRPC_BUILD_TESTING "Build serialrpc's tests" ${SERIALRPC_BUILD_TESTING_DEFAULT})

include(cmake/CPM.cmake)

CPMAddPackage(
  NAME baselib
  GITHUB_REPOSITORY adob/baselib
  VERSION 0.0.0
  GIT_TAG a68d9207428acde15cbd4b96803f317f80e03bd2
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

# Match baselib's strict dialect in both buildtool sources and native consumers.
# Mixing gnu++23 and c++23 changes libstdc++'s __int128 traits across module boundaries.
set(CMAKE_CXX_EXTENSIONS OFF)
buildtool_register_project(serialrpc_sources SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(serialrpc_sources PUBLIC cxx_std_23)
target_link_libraries(serialrpc_sources PUBLIC baselib::baselib Boost::headers)

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

target_include_directories(serialrpc_sources PUBLIC
  $<BUILD_INTERFACE:${SERIALRPC_INCLUDE_DIR}>
  $<BUILD_INTERFACE:${_parent_dir}>
  $<INSTALL_INTERFACE:include>
)

# Buildtool discovers imports and header units from these entry sources.
# Publish the discovered module map because public headers contain imports too.
buildtool_add_library(${PROJECT_NAME}_server
  LIBRARY serialrpc_sources PUBLIC_MODULES
  SOURCES encoding.cc rpc.cc server.cc generated/serialrpc_protocol.pb_msg.cc)
add_library(${PROJECT_NAME}::server ALIAS ${PROJECT_NAME}_server)

buildtool_add_library(${PROJECT_NAME}_client
  LIBRARY serialrpc_sources PUBLIC_MODULES
  SOURCES client.cc internal.cc generated/serialrpc_protocol.pb_client.cc)
add_library(${PROJECT_NAME}::client ALIAS ${PROJECT_NAME}_client)
# The client publishes its own discovered map, which includes its server imports.
# Link the server archive without inheriting a second compiler mapper.
target_link_libraries(${PROJECT_NAME}_client INTERFACE "$<LINK_ONLY:${PROJECT_NAME}::server>")

if(NOT CMAKE_CROSSCOMPILING)
  set(SERIALRPC_PROTOBUF_PROTO_DIR ${protobuf_SOURCE_DIR}/src)
  set(_serialrpc_root_dir ${CMAKE_CURRENT_LIST_DIR})
  add_subdirectory(cmd/serialrpc)
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
  # These files are now inputs to buildtool rather than native CMake sources.
  # Keep protocol generation ahead of either archive's dependency discovery.
  add_custom_target(serialrpc_generate_protocol DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_msg.cc"
    "${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_msg.h"
    "${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_client.cc"
    "${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_client.h"
    "${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_server.cc"
    "${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_server.h")
  add_dependencies(${PROJECT_NAME}_server_build serialrpc_generate_protocol)
  add_dependencies(${PROJECT_NAME}_client_build serialrpc_generate_protocol)
endif()

if(BUILD_TESTING AND SERIALRPC_BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  set(_import_test_output_dir
    ${CMAKE_CURRENT_BINARY_DIR}/import_test_output
  )
  file(MAKE_DIRECTORY ${_import_test_output_dir}/testdata)
  add_custom_command(
    OUTPUT
      ${_import_test_output_dir}/testdata/imported_types.pb_msg.cc
      ${_import_test_output_dir}/testdata/imported_types.pb_msg.h
      ${_import_test_output_dir}/testdata/imported_types.pb_client.cc
      ${_import_test_output_dir}/testdata/imported_types.pb_client.h
      ${_import_test_output_dir}/testdata/imported_types.pb_server.cc
      ${_import_test_output_dir}/testdata/imported_types.pb_server.h
      ${_import_test_output_dir}/testdata/importing_service.pb_msg.cc
      ${_import_test_output_dir}/testdata/importing_service.pb_msg.h
      ${_import_test_output_dir}/testdata/importing_service.pb_client.cc
      ${_import_test_output_dir}/testdata/importing_service.pb_client.h
      ${_import_test_output_dir}/testdata/importing_service.pb_server.cc
      ${_import_test_output_dir}/testdata/importing_service.pb_server.h
    COMMAND ${_protoc_exe}
      --plugin=protoc-gen-serialrpc=$<TARGET_FILE:serialrpcgen_plugin>
      --serialrpc_out=${_import_test_output_dir}
      -I ${CMAKE_CURRENT_LIST_DIR}
      -I ${SERIALRPC_PROTOBUF_PROTO_DIR}
      ${CMAKE_CURRENT_LIST_DIR}/testdata/imported_types.proto
      ${CMAKE_CURRENT_LIST_DIR}/testdata/importing_service.proto
    DEPENDS
      ${CMAKE_CURRENT_LIST_DIR}/testdata/imported_types.proto
      ${CMAKE_CURRENT_LIST_DIR}/testdata/importing_service.proto
      serialrpcgen_plugin
    COMMENT "Generating imported-type regression fixtures"
  )

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

  add_custom_target(serialrpc_generate_tests DEPENDS
    ${_import_test_output_dir}/testdata/imported_types.pb_msg.cc
    ${_import_test_output_dir}/testdata/importing_service.pb_msg.cc
    ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_msg.cc)

  # These sources belong to the serialrpc project, including discovered companions.
  target_include_directories(serialrpc_sources PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/generated
    ${_import_test_output_dir})
  target_link_libraries(serialrpc_sources PRIVATE protobuf::libprotobuf)
  buildtool_add_library(serialrpc_test_sources
    LIBRARY serialrpc_sources
    SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/encoding_test.cc
    ${CMAKE_CURRENT_LIST_DIR}/e2e_test.cc
    ${CMAKE_CURRENT_LIST_DIR}/cmd/serialrpc/call.cc
    ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_msg.cc
    ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_client.cc
    ${CMAKE_CURRENT_LIST_DIR}/generated/example.pb_server.cc
    ${_import_test_output_dir}/testdata/imported_types.pb_msg.cc
    ${_import_test_output_dir}/testdata/importing_service.pb_msg.cc
    ${_import_test_output_dir}/testdata/importing_service.pb_client.cc
    ${_import_test_output_dir}/testdata/importing_service.pb_server.cc
  )

  add_dependencies(serialrpc_test_sources_build serialrpc_generate_tests serialrpc_generate_protocol)
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_link.cc"
    CONTENT "// Test sources are compiled by buildtool.\n")
  add_executable(${PROJECT_NAME}_tests "${CMAKE_CURRENT_BINARY_DIR}/test_link.cc")
  # The runner discovers test functions from binary symbols; retain every test object.
  target_link_libraries(${PROJECT_NAME}_tests PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,serialrpc_test_sources>"
    baselib::test_main
    protobuf::libprotobuf
  )

  add_test(NAME ${PROJECT_NAME}_tests COMMAND ${PROJECT_NAME}_tests)
  set_tests_properties(${PROJECT_NAME}_tests PROPERTIES TIMEOUT 30)

  set(_duplicate_method_id_output_dir
    ${CMAKE_CURRENT_BINARY_DIR}/duplicate_method_id_output
  )
  file(MAKE_DIRECTORY ${_duplicate_method_id_output_dir})
  add_test(
    NAME ${PROJECT_NAME}_rejects_duplicate_method_ids
    COMMAND ${CMAKE_COMMAND}
      -DPROTOC=${_protoc_exe}
      -DPLUGIN=$<TARGET_FILE:serialrpcgen_plugin>
      -DOUTPUT_DIR=${_duplicate_method_id_output_dir}
      -DSERIALRPC_DIR=${CMAKE_CURRENT_LIST_DIR}
      -DPROTOBUF_PROTO_DIR=${SERIALRPC_PROTOBUF_PROTO_DIR}
      -P ${CMAKE_CURRENT_LIST_DIR}/cmake/verify_duplicate_method_id.cmake
  )
  set_tests_properties(${PROJECT_NAME}_rejects_duplicate_method_ids PROPERTIES
    TIMEOUT 30
  )
endif()
