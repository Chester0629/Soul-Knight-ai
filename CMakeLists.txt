cmake_minimum_required(VERSION 3.16)
project(SoulKnight)

set(CMAKE_CXX_STANDARD 17)

# --- Legacy-dependency toolchain shim ---
# CMake >= 4.0 removed compatibility with cmake_minimum_required(VERSION < 3.5).
# Several pinned FetchContent deps still declare it (SDL2 2.26.5, glew 2.2.0,
# glm 0.9.9.8), so configuring under a modern CMake fails without this.
# Setting CMAKE_POLICY_VERSION_MINIMUM lets those legacy CMakeLists configure.
# Remove once the upstream deps are bumped to >= 3.5.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0" AND NOT CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING
        "Minimum policy version for legacy FetchContent deps" FORCE)
endif()

cmake_policy(SET CMP0135 NEW)

# Enable CTest at the top level BEFORE add_subdirectory(PTSD) so the engine's
# gtest_discover_tests() registrations are reachable via `ctest --test-dir build`.
enable_testing()

# PTSD is a vendored, in-tree engine that this project extends in place
# (plan decision #3: generic systems sink into PTSD). We add it as a subdirectory
# rather than via FetchContent.
#
# WHY NOT FetchContent: declaring ptsd with URL=<pinned zip> + SOURCE_DIR=PTSD is
# hazardous here. On a clean reconfigure (empty build/), FetchContent re-extracts
# the pinned archive OVER ${CMAKE_CURRENT_SOURCE_DIR}/PTSD, silently wiping every
# local engine modification (collision, datastore, etc.). add_subdirectory treats
# PTSD as the source it now is. PTSD pulls its own deps (SDL2, glm, nlohmann_json,
# ...) via its internal FetchContent and exports DEPENDENCY_{INCLUDE_DIRS,LINK_LIBRARIES}.
add_subdirectory(PTSD)

include(files.cmake)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
list(TRANSFORM SRC_FILES PREPEND ${SRC_DIR}/)
list(TRANSFORM INCLUDE_FILES PREPEND ${INCLUDE_DIR}/)

# file(GLOB_RECURSE SRC_FILES src/*)
# file(GLOB_RECURSE HEADER_FILES include/*)

add_executable(${PROJECT_NAME}
    ${SRC_DIR}/main.cpp
    ${SRC_FILES}
    ${HEADER_FILES}
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Resources")
else()
    message(FATAL_ERROR "relative RESOURCE_DIR is WIP, Please use `-DCMAKE_BUILD_TYPE=Debug` build for now.")
    # target_compile_definitions(${PROJECT_NAME} PRIVATE RESOURCE_DIR="${CMAKE_INSTALL_PREFIX}/Resources")
endif()

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${DEPENDENCY_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/PTSD/include)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(${PROJECT_NAME}
    SDL2::SDL2main
    PTSD
)
