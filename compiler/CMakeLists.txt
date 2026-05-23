cmake_minimum_required(VERSION 3.16)

add_executable(ta-gcc
    src/ta_gcc.cpp
)
target_include_directories(ta-gcc PRIVATE ${TA64_INCLUDE})
