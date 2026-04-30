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
  GIT_TAG 6470a8516b529194700ccf7eabe503936488b205
)

file(GLOB SOURCES 
    ${CMAKE_CURRENT_LIST_DIR}/*.cc
)

foreach(src_file IN LISTS SOURCES)
  if(src_file MATCHES ".*_(test|posix|experimental|benchmark|arduino)\\.cc$")
    list(REMOVE_ITEM SOURCES ${src_file})
  endif()

  if(src_file MATCHES "(^|[^A-Za-z0-9_])example([^A-Za-z0-9_]|$)")
    list(REMOVE_ITEM SOURCES ${src_file})
  endif()
endforeach()

add_library(${PROJECT_NAME} STATIC
    ${SOURCES}
)

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

get_filename_component(_parent_dir
  "${CMAKE_CURRENT_LIST_DIR}/.."
  REALPATH
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${_parent_dir}>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC baselib::baselib)

set(_serialrpc_root_dir ${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(cmd/serialrpcgen)

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
    ${CMAKE_CURRENT_LIST_DIR}/serialrpc_protocol.proto
  DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/serialrpc_protocol.proto
    serialrpcgen_plugin
  COMMENT "Generating serialrpc_protocol.pb_*.cc/h"
)

target_sources(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/generated/serialrpc_protocol.pb_msg.cc
)

if(BUILD_TESTING)
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
    ${PROJECT_NAME}::${PROJECT_NAME}
    baselib::test_main
  )

  add_test(NAME ${PROJECT_NAME}_tests COMMAND ${PROJECT_NAME}_tests)
  set_tests_properties(${PROJECT_NAME}_tests PROPERTIES TIMEOUT 30)
endif()
