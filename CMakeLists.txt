project(asg)
cmake_minimum_required(VERSION 3.7)
set(CMAKE_CXX_STANDARD 17)

option(OPTION_BUILD_TESTS "Whether to build the tests" TRUE)

if (OPTION_BUILD_TESTS)
    include(gtest.cmake)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(ASG_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

add_library(
    asg-base
    base/AlignedBuf.cpp
    base/Stream.cpp
    base/ring_buffer.c)

target_include_directories(asg-base PUBLIC ${ASG_REPO_ROOT})

add_library(
    asg-client
    client/asg_ring_stream_client.cpp)

target_link_libraries(
    asg-client PRIVATE asg-base)

add_library(
    asg-server
    server/asg_ring_stream_server.cpp)

target_link_libraries(
    asg-server PRIVATE asg-base)
