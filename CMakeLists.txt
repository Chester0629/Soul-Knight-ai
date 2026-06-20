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

# --- Game-layer unit tests (GoogleTest) ---
# Compiles the game logic (SRC_FILES, no main) + the game test files, linked
# against PTSD and gtest_main. Built on demand:
#   cmake --build build --config Debug --target SoulKnightTests
# Run with: ctest --test-dir build -C Debug
if(TEST_FILES)
    include(GoogleTest)
    set(TEST_DIR ${CMAKE_SOURCE_DIR}/test)
    list(TRANSFORM TEST_FILES PREPEND ${TEST_DIR}/)

    add_executable(SoulKnightTests EXCLUDE_FROM_ALL
        ${TEST_FILES}
        ${SRC_FILES}
    )
    if(MSVC)
        target_compile_options(SoulKnightTests PRIVATE /W4)
    else()
        target_compile_options(SoulKnightTests PRIVATE -Wall -Wextra -pedantic)
    endif()
    target_compile_definitions(SoulKnightTests PRIVATE
        RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Resources")
    target_include_directories(SoulKnightTests SYSTEM PRIVATE ${DEPENDENCY_INCLUDE_DIRS})
    target_include_directories(SoulKnightTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/PTSD/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(SoulKnightTests
        PTSD
        GTest::gtest_main
    )
    gtest_discover_tests(SoulKnightTests)
endif()

# --- Standalone HUD bar render-capability check (Phase 2, Path A) ---
# Independent GL verification entry point (NOT wired into Hud.cpp/GameScene).
# Boots a real PTSD context, asserts the baked PNG size contract via
# Util::Image::GetSize(), renders full/naive-half/compensated-half bars, reads the
# framebuffer back to measure the fill edges, and saves a screenshot. Opt-in;
# needs a desktop GL display:
#   cmake --build build --config Debug --target hud_bar_check
#   ./build/Debug/hud_bar_check
add_executable(hud_bar_check EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/hud_bar_check.cpp
    ${SRC_DIR}/ui/HudLayout.cpp
)
if(MSVC)
    target_compile_options(hud_bar_check PRIVATE /W4)
else()
    target_compile_options(hud_bar_check PRIVATE -Wall -Wextra -pedantic)
endif()
target_compile_definitions(hud_bar_check PRIVATE
    RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Resources")
target_include_directories(hud_bar_check SYSTEM PRIVATE ${DEPENDENCY_INCLUDE_DIRS})
target_include_directories(hud_bar_check PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/PTSD/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(hud_bar_check
    SDL2::SDL2main
    PTSD
)

# --- Standalone end-to-end check of the REAL Game::Hud (Phase 3, a4 wiring) ---
# Drives the actual wired Hud class (loads Canvas.layout.json, builds bg + 3 tinted
# bars + numbers) and renders it at full/half/quarter vitals to prove the
# left-anchored bar shrink in the real class + save screenshots. Opt-in; needs GL:
#   cmake --build build --config Debug --target hud_check
#   ./build/Debug/hud_check
add_executable(hud_check EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/hud_check.cpp
    ${SRC_DIR}/ui/Hud.cpp
    ${SRC_DIR}/ui/HudLayout.cpp
)
if(MSVC)
    target_compile_options(hud_check PRIVATE /W4)
else()
    target_compile_options(hud_check PRIVATE -Wall -Wextra -pedantic)
endif()
target_compile_definitions(hud_check PRIVATE
    RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Resources")
target_include_directories(hud_check SYSTEM PRIVATE ${DEPENDENCY_INCLUDE_DIRS})
target_include_directories(hud_check PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/PTSD/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(hud_check
    SDL2::SDL2main
    PTSD
)

# --- Warning-suppression define, scoped to the game-layer targets only ---
# MSVC 19.44+ (VS 2022 17.14) deprecated stdext::checked_array_iterator (STL4043),
# which the vendored spdlog/fmt and std::copy-into-raw-pointer instantiate. Under
# /W4 that floods every logger-using TU with C4996 (verified: an untouched
# src/entities/Player.cpp emits 225). This is MSVC's documented suppression for
# exactly that deprecation -- notice-only, zero codegen impact. It is applied
# PER-TARGET to only the targets we own and need clean (game + tests + tools that
# compile our TUs against vendored spdlog/fmt), NOT globally: a global
# add_compile_definitions would change the compile of vendored PTSD + every fetched
# dep + any future target, which is out of scope for keeping OUR builds clean. The
# vendored PTSD library keeps its own (pre-existing) warning behaviour untouched.
# Remove once spdlog/fmt drop the deprecated API.
foreach(_clean_target SoulKnight SoulKnightTests hud_bar_check hud_check)
    if(TARGET ${_clean_target})
        target_compile_definitions(${_clean_target} PRIVATE
            _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
    endif()
endforeach()
