cmake_minimum_required(VERSION 3.14)
project(extract_fx)
set(CMAKE_CXX_STANDARD 23)

include (fx_sources.cmake)

# Build the extract_fx executable (with builtin unit testing)
add_executable(extract_fx extract_fx.cpp)

# Example executable which uses the target_fx_File macro to add two cpp files which are to be 
# passed through extract_fx.
add_executable(format_literal_test format_literal.h)
target_include_directories(format_literal_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_fx_sources(format_literal_test format_literal_test.cpp extratest.cpp)

# Setup for ctest
enable_testing()
add_test(NAME test_fx COMMAND extract_fx --test)
