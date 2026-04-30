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
  GIT_TAG 36f6c17ea5fcf481d9d6849566a980ca82b7d6e3
)

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

set(SERIALRPC_BOOST_VERSION "1.87.0" CACHE STRING "Boost version used by serialrpc")
set(SERIALRPC_BOOST_COMPONENTS "unordered" CACHE STRING "Boost components required by serialrpc")

CPMFindPackage(
  NAME Boost
  VERSION ${SERIALRPC_BOOST_VERSION} # Versions less than 1.85.0 may need patches for installation targets.
  URL https://github.com/boostorg/boost/releases/download/boost-${SERIALRPC_BOOST_COMPONENTS}/boost-${SERIALRPC_BOOST_COMPONENTS}-cmake.tar.xz
  URL_HASH SHA256=7da75f171837577a52bbf217e17f8ea576c7c246e4594d617bfde7fafd408be5
  FIND_PACKAGE_ARGUMENTS "CONFIG;COMPONENTS;${SERIALRPC_BOOST_COMPONENTS}"
  OPTIONS "BOOST_ENABLE_CMAKE ON" "BOOST_SKIP_INSTALL_RULES ON" # Set `OFF` for installation
          "BUILD_SHARED_LIBS OFF"
)

set(SERIALRPC_PROTOBUF_PROTO_DIR ${protobuf_SOURCE_DIR}/src)

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

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${SERIALRPC_INCLUDE_DIR}>
  $<BUILD_INTERFACE:${_parent_dir}>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC baselib::baselib)

set(SERIALRPC_BOOST_TARGETS Boost::headers)
foreach(boost_component IN LISTS SERIALRPC_BOOST_COMPONENTS)
  if(TARGET "Boost::${boost_component}")
    list(APPEND SERIALRPC_BOOST_TARGETS "Boost::${boost_component}")
  endif()
endforeach()
target_link_libraries(${PROJECT_NAME} PUBLIC ${SERIALRPC_BOOST_TARGETS})

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
    ${PROJECT_NAME}::${PROJECT_NAME}
    baselib::test_main
  )

  add_test(NAME ${PROJECT_NAME}_tests COMMAND ${PROJECT_NAME}_tests)
  set_tests_properties(${PROJECT_NAME}_tests PROPERTIES TIMEOUT 30)
endif()
