file(GLOB_RECURSE PILOT_RUNTIME_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/comm/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/conf/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/intermediate/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/parallel/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/system/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/utils/src/*.cpp")
add_library(pilot_runtime STATIC ${PILOT_RUNTIME_SOURCES})
target_include_directories(pilot_runtime PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/comm/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/conf/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/intermediate/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/parallel/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/system/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/utils/include")
target_link_libraries(pilot_runtime PUBLIC MPI::MPI_CXX)
