cmake_minimum_required(VERSION 3.16)

# ── Disassembler ─────────────────────────────────────────────
add_executable(ta-dasm
    src/ta_dasm.cpp
)
target_include_directories(ta-dasm PRIVATE ${TA64_INCLUDE})

# ── Interactive monitor / debugger ────────────────────────────
add_executable(ta-monitor
    src/ta_monitor.cpp
)
target_include_directories(ta-monitor PRIVATE ${TA64_INCLUDE})

# ── Object file inspector ────────────────────────────────────
add_executable(ta-objdump
    src/ta_objdump.cpp
)
target_include_directories(ta-objdump PRIVATE ${TA64_INCLUDE})
