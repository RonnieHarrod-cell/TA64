# TA64 — Custom 64-bit Architecture Ecosystem

```
  _____ _   _  __   _  _
 |_   _/ \ / |/ /_ | || |
   | |/ _ V / _ \| || |
   |_/_/ \_/_/ \_\_||_|
   Custom 64-bit RISC Platform
```

A complete miniature computing platform: CPU emulator, assembler, linker, compiler frontend, binary format, kernel, and debugger — all in modern C++20.

---

## What's Included

| Component    | Binary       | Description                                     |
|--------------|--------------|-------------------------------------------------|
| Emulator     | `ta64-emu`   | Full CPU emulator with SDL2 terminal display    |
| Assembler    | `ta-asm`     | Two-pass assembler with labels, macros, sections|
| Linker       | `ta-ld`      | Links multiple object files into one TAEXE      |
| Compiler     | `ta-gcc`     | Tiny C subset → TA64 assembly                   |
| Disassembler | `ta-dasm`    | Pretty-prints TAEXE binaries                    |
| Debugger     | `ta-monitor` | Interactive GDB-like monitor with breakpoints   |

---

## Quick Start

### Prerequisites

```bash
# Ubuntu / Debian
sudo apt-get install cmake build-essential

# Optional: SDL2 for graphical terminal window
sudo apt-get install libsdl2-dev libsdl2-ttf-dev
```

### Build

```bash
git clone <repo-url> TA64
cd TA64
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

All binaries land in `build/`.

---

## Usage

### Run a TAEXE binary

```bash
./ta64-emu examples/hello.taexe
./ta64-emu examples/hello.taexe --trace       # show each instruction
./ta64-emu examples/hello.taexe -r            # dump registers on exit
./ta64-emu examples/hello.taexe -d 256        # memory dump first 256 bytes
./ta64-emu examples/hello.taexe --display     # SDL2 terminal window
```

### Assemble

```bash
./ta-asm examples/hello.asm hello.taexe
./ta-asm examples/arithmetic.asm arithmetic.taexe
```

### Link multiple objects

```bash
./ta-asm lib.asm lib.taexe
./ta-asm main.asm main.taexe
./ta-ld -o program.taexe main.taexe lib.taexe
```

### Compile C → TA64

```bash
# C → assembly
./ta-gcc examples/example.c -o example.asm

# Assembly → binary
./ta-asm example.asm example.taexe

# Run it
./ta64-emu example.taexe
```

### Full pipeline

```bash
./ta-gcc myprogram.c -o myprogram.asm   # compile
./ta-asm myprogram.asm myprogram.taexe  # assemble
./ta64-emu myprogram.taexe              # run
```

### Disassemble a binary

```bash
./ta-dasm hello.taexe            # disassemble .text
./ta-dasm hello.taexe -a         # also dump .data
```

### Interactive Debugger

```bash
./ta-monitor hello.taexe
```

Monitor commands:
```
(ta-mon) regs              # dump all registers
(ta-mon) dis 1000          # disassemble from 0x1000
(ta-mon) step 5            # execute 5 instructions
(ta-mon) break 1010        # set breakpoint at 0x1010
(ta-mon) run               # run until HLT or breakpoint
(ta-mon) mem 1000 128      # hex dump 128 bytes from 0x1000
(ta-mon) setreg R0 FF      # set R0 = 0xFF
(ta-mon) trace on          # enable instruction trace
(ta-mon) quit
```

---

## Assembly Quick Reference

```asm
.section .data
msg:    .string "Hello!\n"

.section .text

_start:
    ; MOV: register or immediate
    MOV  R0, #5          ; R0 = 5  (imm8)
    MOVI R1, 1000        ; R1 = 1000 (imm16)
    MOV  R2, R0          ; R2 = R0

    ; Arithmetic
    ADD  R0, R1          ; R0 += R1
    SUB  R0, R1          ; R0 -= R1
    MUL  R0, R1          ; R0 *= R1
    DIV  R0, R1          ; R0 /= R1
    INC  R0              ; R0++
    DEC  R0              ; R0--

    ; Bitwise
    AND  R0, R1
    OR   R0, R1
    XOR  R0, R1
    SHL  R0, R1          ; R0 <<= R1
    SHR  R0, R1          ; R0 >>= R1

    ; Memory
    LOAD  R0, [R1]        ; R0 = Mem[R1]
    LOAD  R0, [R1 + 8]    ; R0 = Mem[R1 + 8]
    STORE R0, [R1]        ; Mem[R1] = R0
    PUSH  R0
    POP   R0
    LEA   R0, msg         ; R0 = address of msg

    ; Control flow
    CMP  R0, R1           ; set flags
    JE   equal_label
    JNE  not_equal
    JLT  less_than
    JGT  greater_than
    JMP  somewhere
    CALL my_function
    RET

    ; Syscall
    MOV  R0, #1           ; SYS_WRITE
    MOV  R1, #1           ; fd=stdout
    LEA  R2, msg          ; buffer
    MOVI R3, 7            ; length
    INT  0x80

    HLT
```

---

## C Subset Supported by ta-gcc

```c
int add(int a, int b) {
    return a + b;
}

int main() {
    int x = 5;
    int y = 10;

    // if / else
    if (x < y) {
        x = x + 1;
    } else {
        y = y - 1;
    }

    // while loop
    int i = 0;
    while (i < 10) {
        x = x + i;
        ++i;
    }

    // function calls
    int result = add(x, y);
    return result;
}
```

Not yet supported: pointers, arrays, strings, structs, `for` loops, `printf`. These are left as extension exercises.

---

## TAEXE Binary Format

```
Offset  Size  Field
------  ----  -----
0       4     Magic bytes: 'T' 'A' '6' '4'
4       4     Version (1)
8       8     Entry point (VA)
16      8     .text section file offset
24      8     .text size
32      8     .data section file offset
40      8     .data size
48      8     .bss size (zero-inited, not stored in file)
56      8     Load address
--- 64 bytes total header ---
[text bytes]
[data bytes]
```

---

## Project Structure

```
TA64/
├── include/
│   ├── ta64_isa.hpp     ← ISA definitions, opcodes, encoding helpers
│   ├── memory.hpp       ← 1MB RAM emulation, read/write8..64
│   ├── cpu.hpp          ← CPU class: registers, fetch/decode/execute
│   └── loader.hpp       ← TAEXE loader / writer
├── emulator/
│   └── src/main.cpp     ← ta64-emu entry, SDL2 terminal
├── assembler/
│   └── src/ta_asm.cpp   ← Two-pass assembler
├── linker/
│   └── src/ta_ld.cpp    ← Linker
├── compiler/
│   └── src/ta_gcc.cpp   ← Tiny C compiler frontend
├── tools/
│   └── src/
│       ├── ta_dasm.cpp  ← Disassembler
│       └── ta_monitor.cpp ← Interactive debugger
├── kernel/
│   └── kernel.asm       ← Example micro-kernel
├── examples/
│   ├── hello.asm        ← Hello world
│   ├── arithmetic.asm   ← ALU demo
│   └── example.c        ← C source for ta-gcc
├── docs/
│   └── ARCHITECTURE.md  ← Full ISA reference
├── CMakeLists.txt
└── README.md
```

---

## Extending TA64

### Adding a new instruction

1. Add opcode to `Opcode` enum in `include/ta64_isa.hpp`
2. Add entry to `OPCODE_TABLE` for disassembler
3. Add case to `CPU::execute()` in `include/cpu.hpp`
4. Add parsing in `TwoPassAssembler::process_line()` in assembler
5. Add code-gen in `CodeGen` if compiler should emit it

### Expanding memory

Change `Memory::SIZE` in `include/memory.hpp` and update `DEFAULT_STACK_TOP` / `MEM_SIZE` in `ta64_isa.hpp`.

### Adding syscalls

Extend the switch in the `SyscallHandler` lambda in `emulator/src/main.cpp` and add the constant to `Syscall::` namespace in `ta64_isa.hpp`.

---

## Architecture Summary

- **64-bit**, little-endian
- **Fixed 4-byte instructions** — simple fetch/decode
- **16 GPRs** (R0–R15) + SP, PC, FL
- **Flat memory** — no virtual memory, no MMU (yet)
- **Software interrupts** via `INT 0x80` for syscalls
- **RISC discipline** — load/store architecture, no complex addressing

---

## License

MIT License — free to use, modify, and extend for educational and hobby purposes.
