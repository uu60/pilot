cmake_minimum_required(VERSION 3.18)
project(pilot LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(MPI REQUIRED)
find_package(OpenSSL REQUIRED)

add_subdirectory(runtime)
add_subdirectory(primitive)
add_subdirectory(db)
add_subdirectory(launcher)
add_subdirectory(switch_simulator)

add_executable(pilot main.cpp)
target_link_libraries(pilot PRIVATE pilot_db)
