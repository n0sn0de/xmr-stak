cmake_minimum_required(VERSION 3.18)
project(n0s-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Golden hash constants verification test
add_executable(test_constants test_constants.cpp)
target_include_directories(test_constants PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
