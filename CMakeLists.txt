cmake_minimum_required(VERSION 3.16)
project(TA64 VERSION 1.0.0 LANGUAGES CXX)

# ── C++20 everywhere ─────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type default ───────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ── Compiler warnings ────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g3 -fno-omit-frame-pointer)
endif()

# ── Shared include path ───────────────────────────────────────
set(TA64_INCLUDE ${CMAKE_SOURCE_DIR}/include)

# ── Optional SDL2 for emulator display ───────────────────────
find_package(SDL2 QUIET)
find_package(SDL2_ttf QUIET)

# ── Sub-targets ──────────────────────────────────────────────
add_subdirectory(emulator)
add_subdirectory(assembler)
add_subdirectory(linker)
add_subdirectory(compiler)
add_subdirectory(tools)

# ── Install rules ────────────────────────────────────────────
install(TARGETS ta64-emu ta-asm ta-ld ta-gcc ta-dasm ta-monitor ta-objdump
        RUNTIME DESTINATION bin)

# ── Summary ──────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== TA64 Build Configuration ===")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ std    : ${CMAKE_CXX_STANDARD}")
if(SDL2_FOUND AND SDL2_ttf_FOUND)
    message(STATUS "  SDL2       : YES (display mode enabled)")
else()
    message(STATUS "  SDL2       : NO  (console-only mode)")
endif()
message(STATUS "")
