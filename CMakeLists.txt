cmake_minimum_required(VERSION 3.16)

get_filename_component(
  CURRENT_DIRNAME
  ${CMAKE_CURRENT_LIST_DIR}
  NAME
)

project(${CURRENT_DIRNAME} VERSION 0.0.0 LANGUAGES CXX)

include(cmake/CPM.cmake)
CPMUsePackageLock(package-lock.cmake)

#CPMAddPackage("gh:adob/baselib#master")
CPMAddPackage(
  NAME baselib
  GITHUB_REPOSITORY adob/baselib
  VERSION 0.0.0
  GIT_TAG c6ea2af91ee819a343e37c8f122ec350c065bb96
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
#set_target_properties(baselib PROPERTIES
  #CXX_STANDARD 26
  #CXX_STANDARD_REQUIRED YES
  #CXX_EXTENSIONS NO
#)

get_filename_component(_parent_dir
  "${CMAKE_CURRENT_LIST_DIR}/.."
  REALPATH
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${_parent_dir}>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC baselib::baselib)
