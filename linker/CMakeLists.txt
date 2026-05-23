cmake_minimum_required(VERSION 3.16)

add_executable(ta-ld
    src/ta_ld.cpp
)
target_include_directories(ta-ld PRIVATE ${TA64_INCLUDE})
