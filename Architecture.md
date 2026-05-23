# TA64 Architecture Reference

## Overview

TA64 is a 64-bit RISC-style ISA designed for educational OS development. It draws inspiration from ARM64 and RISC-V but is deliberately simplified for clarity and hackability.

---

## Characteristics

| Property        | Value                      |
|-----------------|----------------------------|
| Word size       | 64-bit                     |
| Endianness      | Little-endian              |
| Instruction size| Fixed 4 bytes              |
| Address space   | Flat 64-bit (1 MB emulated)|
| Privilege model | Single-level (kernel only) |
| Calling conv.   | Args: R0–R7, Return: R0    |

---

## Registers

| Name      | Index | Purpose                          |
|-----------|-------|----------------------------------|
| R0–R15    | 0–15  | General purpose                  |
| SP        | 0x10  | Stack pointer (grows downward)   |
| PC        | 0x11  | Program counter                  |
| FL        | 0x12  | Flags register                   |

### Flags Register (FL)

| Bit | Name     | Set when                        |
|-----|----------|---------------------------------|
| 0   | ZERO     | Result == 0                     |
| 1   | NEGATIVE | Result bit 63 set               |
| 2   | CARRY    | Unsigned overflow               |
| 3   | OVERFLOW | Signed overflow                 |
| 31  | HALT     | CPU stopped                     |

### Calling Convention

- **Caller** places arguments in R0, R1, R2 … R7
- **Callee** returns result in R0
- **Caller-saved**: R0–R7 (may be clobbered by callee)
- **Callee-saved**: R8–R15, SP, R13 (frame pointer by convention)
- Frame pointer: R13 (convention, not enforced by hardware)

---

## Instruction Encoding

Every instruction is exactly **4 bytes** in little-endian order:

```
Byte 0  : OPCODE
Byte 1  : ARG1
Byte 2  : ARG2
Byte 3  : ARG3
```

### Register field

- Lower 4 bits of ARG1/ARG2/ARG3 = register index (0–15)
- **Bit 7** of ARG1 (`0x80`) = **immediate flag**

### Immediate encodings

| Format   | Instruction | ARG1         | ARG2   | ARG3   |
|----------|-------------|--------------|--------|--------|
| imm8     | MOV         | dst \| 0x80  | imm8   | 0      |
| imm16    | MOVI        | dst          | lo8    | hi8    |
| simm16   | JMP/Jcc/CALL| src \| 0x80  | lo8    | hi8    |
| offset8  | LOAD/STORE  | reg          | base   | off8   |

---

## Instruction Set

### Arithmetic

| Mnemonic         | Operation                      | Flags    |
|------------------|--------------------------------|----------|
| `NOP`            | No operation                   | —        |
| `MOV Rd, Rs`     | Rd = Rs                        | ZN       |
| `MOV Rd, #imm8`  | Rd = imm8                      | ZN       |
| `MOVI Rd, #imm16`| Rd = imm16                     | ZN       |
| `ADD Rd, Rs`     | Rd = Rd + Rs                   | ZNCO     |
| `SUB Rd, Rs`     | Rd = Rd − Rs                   | ZNCO     |
| `MUL Rd, Rs`     | Rd = Rd × Rs                   | ZN       |
| `DIV Rd, Rs`     | Rd = Rd ÷ Rs                   | ZN       |
| `MOD Rd, Rs`     | Rd = Rd mod Rs                 | ZN       |
| `INC Rd`         | Rd = Rd + 1                    | ZN       |
| `DEC Rd`         | Rd = Rd − 1                    | ZN       |
| `AND Rd, Rs`     | Rd = Rd & Rs                   | ZN       |
| `OR  Rd, Rs`     | Rd = Rd \| Rs                  | ZN       |
| `XOR Rd, Rs`     | Rd = Rd ^ Rs                   | ZN       |
| `NOT Rd`         | Rd = ~Rd                       | ZN       |
| `SHL Rd, Rs`     | Rd = Rd << Rs                  | ZN       |
| `SHR Rd, Rs`     | Rd = Rd >> Rs (logical)        | ZN       |

### Memory

| Mnemonic               | Operation                          |
|------------------------|------------------------------------|
| `LOAD  Rd, [Rb + off8]`| Rd = Mem64[Rb + sign_ext(off8)]    |
| `STORE Rs, [Rb + off8]`| Mem64[Rb + sign_ext(off8)] = Rs    |
| `PUSH  Rs`             | SP -= 8; Mem64[SP] = Rs            |
| `POP   Rd`             | Rd = Mem64[SP]; SP += 8            |
| `LEA   Rd, label`      | Rd = absolute address of label     |

### Control Flow

| Mnemonic         | Condition                      |
|------------------|-------------------------------|
| `JMP target`     | Unconditional                  |
| `CMP Ra, Rb`     | Set flags from Ra − Rb         |
| `JE  target`     | ZERO == 1                      |
| `JNE target`     | ZERO == 0                      |
| `JLT target`     | NEGATIVE == 1                  |
| `JGT target`     | ZERO==0 && NEGATIVE==0         |
| `JLE target`     | ZERO==1 \|\| NEGATIVE==1       |
| `JGE target`     | NEGATIVE == 0                  |
| `CALL target`    | Push PC+4, jump to target      |
| `RET`            | Pop PC                         |

Branch targets may be a label (resolved to ±32767 instruction offsets, i.e. ±128KB) or a register (`JMP R3`).

### System

| Mnemonic   | Operation                                  |
|------------|--------------------------------------------|
| `INT n`    | Software interrupt — dispatches to handler |
| `HLT`      | Set HALT flag, stop CPU                    |
| `IRET`     | Pop FL then PC (return from interrupt)     |

---

## Syscall ABI (INT 0x80)

Call number in **R0**. Arguments in R1, R2, R3 …

| R0  | Name       | Args                          | Return      |
|-----|------------|-------------------------------|-------------|
| 0   | SYS_EXIT   | R1 = exit code                | —           |
| 1   | SYS_WRITE  | R1=fd, R2=buf, R3=len         | R0 = bytes  |
| 2   | SYS_READ   | R1=fd, R2=buf, R3=len         | R0 = bytes  |
| 3   | SYS_YIELD  | —                             | —           |
| 4   | SYS_GETPID | —                             | R0 = PID    |
| 5   | SYS_TIME   | —                             | R0 = ticks  |

---

## TAEXE Binary Format

```
Offset  Size  Field
0       4     Magic: 'T','A','6','4'
4       4     Version (= 1)
8       8     Entry point (virtual address)
16      8     .text file offset
24      8     .text size (bytes)
32      8     .data file offset
40      8     .data size (bytes)
48      8     .bss size (zero-init, not stored)
56      8     Load address (default 0x1000)
```

Total header size: **64 bytes**.

---

## Memory Map

```
0x00000000  Reserved (null page)
0x00001000  .text (code) — load address
   ...
0x000XXXXX  .data (initialised)
   ...
0x000YYYYY  .bss  (zero-initialised)
   ...
0x000EF000  ↓ Stack (grows down from 0xFF000)
0x000FF000  Stack top (initial SP)
0x000FFFFF  Top of 1 MB RAM
```

---

## Assembly Syntax

```asm
; Comment
.section .text         ; or .data / .bss

.equ CONST, 42         ; constant definition

label:                 ; label definition
    MOV  R0, #5        ; immediate (8-bit)
    MOVI R1, 1000      ; immediate (16-bit)
    ADD  R0, R1        ; register-register
    PUSH R0
    CALL subroutine
    INT  0x80          ; syscall

.section .data
msg:
    .string "hello\n"
    .byte  0x41, 0x42  ; raw bytes
    .word  1234        ; 16-bit
    .qword 0xDEADBEEF  ; 64-bit
```

---

## Toolchain Pipeline

```
foo.c
  │  ta-gcc foo.c -o foo.asm      (C → assembly)
  ▼
foo.asm
  │  ta-asm foo.asm foo.taexe     (assembly → TAEXE)
  ▼
foo.taexe
  │  ta-ld -o final.taexe a.taexe b.taexe   (link)
  ▼
final.taexe
  │  ta64-emu final.taexe         (run)
  ▼
  output
```

---

## Toolchain Reference

| Tool        | Purpose                                      |
|-------------|----------------------------------------------|
| `ta64-emu`  | CPU emulator, load and run TAEXE binaries    |
| `ta-asm`    | Two-pass assembler, produces TAEXE           |
| `ta-ld`     | Linker, merges multiple TAEXE objects        |
| `ta-gcc`    | Tiny C frontend, emits TA64 assembly         |
| `ta-dasm`   | Disassembler, pretty-prints TAEXE binaries   |
| `ta-monitor`| Interactive debugger with breakpoints/step   |