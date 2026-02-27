cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

set(PROJECT_NAME ArchiEngine)
project(${PROJECT_NAME})

file(GLOB_RECURSE ARCHI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_executable(${PROJECT_NAME} ${ARCHI_SOURCES})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(external/glfw)
target_link_libraries(${PROJECT_NAME} glfw)

add_subdirectory(external/glad)
target_link_libraries(${PROJECT_NAME} glad)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include"
)

# Copy runtime assets next to the executable (for shader hot reload etc.).
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
)