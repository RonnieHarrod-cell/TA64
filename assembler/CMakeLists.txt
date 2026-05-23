cmake_minimum_required(VERSION 3.16)

add_executable(ta-asm
    src/ta_asm.cpp
)
target_include_directories(ta-asm PRIVATE ${TA64_INCLUDE})
