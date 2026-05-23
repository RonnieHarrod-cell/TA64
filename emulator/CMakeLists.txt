cmake_minimum_required(VERSION 3.16)

add_executable(ta64-emu
    src/main.cpp
)

target_include_directories(ta64-emu PRIVATE ${TA64_INCLUDE})

if(SDL2_FOUND AND SDL2_ttf_FOUND)
    target_compile_definitions(ta64-emu PRIVATE HAVE_SDL2)
    target_link_libraries(ta64-emu PRIVATE SDL2::SDL2 SDL2_ttf::SDL2_ttf)
    message(STATUS "  [emulator] Building with SDL2 display support")
endif()

set_target_properties(ta64-emu PROPERTIES OUTPUT_NAME "ta64-emu")
