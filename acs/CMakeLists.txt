cmake_minimum_required(VERSION 3.10)
project(acs VERSION 1.0.0)

set(CMAKE_C_STANDARD_REQUIRED true)
set(CMAKE_C_STANDARD 23)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)
configure_file(${CMAKE_SOURCE_DIR}/include/acs.h.in acs.h)

file(GLOB SOURCES "src/*.c")
add_executable(acs ${SOURCES})

target_include_directories(acs PUBLIC "${PROJECT_BINARY_DIR}")
target_link_libraries(acs PRIVATE Threads::Threads)