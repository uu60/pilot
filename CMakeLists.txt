cmake_minimum_required(VERSION 4.2)
project(pilot)

set(CMAKE_CXX_STANDARD 20)

# Set base optimization flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")


find_package(MPI REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED CONFIG)

add_subdirectory(runtime)
add_subdirectory(primitives)
add_subdirectory(db)
add_subdirectory(db/benchmark)
