cmake_minimum_required(VERSION 3.16)
project(ThreadPool CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Library
add_library(threadpool ThreadPool.cpp)
target_include_directories(threadpool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Google Test via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# Prevent overriding the parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Test executable
enable_testing()

add_executable(threadpool_test test_threadpool.cpp)
target_link_libraries(threadpool_test PRIVATE threadpool GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(threadpool_test)

if(MSVC)
    target_compile_options(threadpool_test PRIVATE /W4)
else()
    target_compile_options(threadpool_test PRIVATE -Wall -Wextra -pthread)
    target_link_options(threadpool_test PRIVATE -pthread)
endif()
