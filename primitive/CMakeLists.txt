file(GLOB_RECURSE PILOT_PRIMITIVE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(pilot_primitive STATIC ${PILOT_PRIMITIVE_SOURCES})
target_include_directories(pilot_primitive PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(pilot_primitive PUBLIC pilot_runtime)
