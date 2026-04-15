cmake_minimum_required(VERSION 3.25)

project(hyprmacs-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(HYPRMACS_BUILD_TESTS "Build bootstrap tests for the hyprmacs plugin." ON)

add_library(hyprmacs SHARED
  src/plugin_main.cpp
)

set_target_properties(hyprmacs PROPERTIES
  OUTPUT_NAME "hyprmacs"
  PREFIX ""
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(hyprmacs PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS hyprmacs
  LIBRARY DESTINATION lib
)

enable_testing()

if(HYPRMACS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
