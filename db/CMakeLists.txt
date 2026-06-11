file(GLOB_RECURSE PILOT_DB_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(pilot_db STATIC ${PILOT_DB_SOURCES})
target_include_directories(pilot_db PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(pilot_db PUBLIC pilot_primitive)

add_subdirectory(benchmark)
