#pragma once
// ============================================================
//  TA64 — Instruction Set Architecture definitions
//  Shared by emulator, assembler, linker, compiler, tools
// ============================================================
#include <cstdint>
#include <string_view>
#include <array>

namespace TA64 {

// ─── Instruction layout ────────────────────────────────────
//  Each instruction is exactly 4 bytes (little-endian):
//   Byte 0 : OPCODE
//   Byte 1 : ARG1   (register / immediate high bits)
//   Byte 2 : ARG2
//   Byte 3 : ARG3
// ────────────────────────────────────────────────────────────

enum class Opcode : uint8_t {
    // Arithmetic
    NOP  = 0x00,
    MOV  = 0x01,   // MOV  Rdst, Rsrc | MOV Rdst, #imm8
    ADD  = 0x02,   // ADD  Rdst, Rsrc
    SUB  = 0x03,
    MUL  = 0x04,
    DIV  = 0x05,
    INC  = 0x06,   // INC  Rdst
    DEC  = 0x07,
    AND  = 0x08,
    OR   = 0x09,
    XOR  = 0x0A,
    NOT  = 0x0B,
    SHL  = 0x0C,
    SHR  = 0x0D,
    MOD  = 0x0E,
    MOVI = 0x0F,   // MOV  Rdst, #imm16  (ARG2|ARG3 = 16-bit immediate)

    // Memory
    LOAD  = 0x10,  // LOAD  Rdst, [Rbase + offset8]
    STORE = 0x11,  // STORE Rsrc, [Rbase + offset8]
    PUSH  = 0x12,  // PUSH  Rsrc
    POP   = 0x13,  // POP   Rdst
    LEA   = 0x14,  // LEA   Rdst, label_offset (2-byte imm)

    // Control flow
    JMP  = 0x20,   // JMP  Rreg | JMP  #offset (signed 16-bit in ARG2:ARG3)
    CMP  = 0x21,   // CMP  Ra, Rb  → sets FL
    JE   = 0x22,   // Jump if ZERO flag set
    JNE  = 0x23,
    JLT  = 0x24,
    JGT  = 0x25,
    JLE  = 0x26,
    JGE  = 0x27,
    CALL = 0x28,   // CALL #offset or CALL Rreg
    RET  = 0x29,

    // System
    INT  = 0x30,   // INT  #vector
    HLT  = 0x31,
    IRET = 0x32,   // Return from interrupt

    INVALID = 0xFF
};

// ─── Registers ─────────────────────────────────────────────
enum class Reg : uint8_t {
    R0=0, R1, R2, R3, R4, R5, R6, R7,
    R8, R9, R10, R11, R12, R13, R14, R15,
    SP=0x10,  // Stack pointer (distinct encoding)
    PC=0x11,  // Program counter
    FL=0x12,  // Flags
    NONE=0xFF
};

constexpr uint8_t REG_COUNT = 16;
constexpr uint8_t SP_IDX    = 0x10;
constexpr uint8_t PC_IDX    = 0x11;
constexpr uint8_t FL_IDX    = 0x12;

// ─── Flags register bits ───────────────────────────────────
namespace Flags {
    constexpr uint64_t ZERO     = 1ULL << 0;
    constexpr uint64_t NEGATIVE = 1ULL << 1;
    constexpr uint64_t CARRY    = 1ULL << 2;
    constexpr uint64_t OVERFLOW = 1ULL << 3;
    constexpr uint64_t HALT     = 1ULL << 31;  // CPU halted
}

// ─── Addressing mode flag in ARG1 ─────────────────────────
constexpr uint8_t IMM_FLAG = 0x80;   // Bit 7 of ARG1 → immediate mode

// ─── TAEXE binary format ──────────────────────────────────
struct TAEXEHeader {
    uint8_t  magic[4]   = {'T','A','6','4'};
    uint32_t version    = 1;
    uint64_t entry_point;    // Virtual address of _start
    uint64_t text_offset;    // File offset of .text
    uint64_t text_size;
    uint64_t data_offset;    // File offset of .data
    uint64_t data_size;
    uint64_t bss_size;       // Size of zero-initialised segment
    uint64_t load_address;   // Preferred load address (default 0x1000)
};

constexpr uint64_t DEFAULT_LOAD_ADDR  = 0x00001000;
constexpr uint64_t DEFAULT_STACK_TOP  = 0x000FF000;
constexpr uint64_t MEM_SIZE           = 0x00100000; // 1 MB

// ─── Syscall numbers (INT 0x80) ────────────────────────────
namespace Syscall {
    constexpr uint64_t SYS_EXIT   = 0;
    constexpr uint64_t SYS_WRITE  = 1;
    constexpr uint64_t SYS_READ   = 2;
    constexpr uint64_t SYS_YIELD  = 3;
    constexpr uint64_t SYS_GETPID = 4;
    constexpr uint64_t SYS_TIME   = 5;
}

// ─── Helpers ───────────────────────────────────────────────
constexpr uint32_t encode_instruction(Opcode op, uint8_t a1, uint8_t a2, uint8_t a3) noexcept {
    return (static_cast<uint32_t>(a3) << 24)
         | (static_cast<uint32_t>(a2) << 16)
         | (static_cast<uint32_t>(a1) <<  8)
         | static_cast<uint32_t>(op);
}

struct Instruction {
    Opcode  opcode;
    uint8_t arg1, arg2, arg3;

    static Instruction decode(uint32_t raw) noexcept {
        return {
            static_cast<Opcode>(raw & 0xFF),
            static_cast<uint8_t>((raw >>  8) & 0xFF),
            static_cast<uint8_t>((raw >> 16) & 0xFF),
            static_cast<uint8_t>((raw >> 24) & 0xFF)
        };
    }

    [[nodiscard]] uint16_t imm16() const noexcept {
        return static_cast<uint16_t>(arg2) | (static_cast<uint16_t>(arg3) << 8);
    }
    [[nodiscard]] int16_t simm16() const noexcept {
        return static_cast<int16_t>(imm16());
    }
    [[nodiscard]] bool arg1_is_imm() const noexcept { return (arg1 & IMM_FLAG) != 0; }
    // Full register index (0x00-0x0F = R0-R15, 0x10=SP, 0x11=PC, 0x12=FL)
    // Bit 7 is the IMM_FLAG, bits 0-6 are the register index
    [[nodiscard]] uint8_t reg1() const noexcept { return arg1 & 0x7F; }
    [[nodiscard]] uint8_t reg2() const noexcept { return arg2 & 0x7F; }
    [[nodiscard]] uint8_t reg3() const noexcept { return arg3 & 0x7F; }
};

// ─── Mnemonic table (for disassembler / assembler) ─────────
struct OpcodeInfo { Opcode op; std::string_view name; };
constexpr std::array<OpcodeInfo, 36> OPCODE_TABLE = {{
    {Opcode::NOP,  "NOP"},  {Opcode::MOV,  "MOV"},  {Opcode::MOVI, "MOVI"},
    {Opcode::ADD,  "ADD"},  {Opcode::SUB,  "SUB"},  {Opcode::MUL,  "MUL"},
    {Opcode::DIV,  "DIV"},  {Opcode::INC,  "INC"},  {Opcode::DEC,  "DEC"},
    {Opcode::AND,  "AND"},  {Opcode::OR,   "OR"},   {Opcode::XOR,  "XOR"},
    {Opcode::NOT,  "NOT"},  {Opcode::SHL,  "SHL"},  {Opcode::SHR,  "SHR"},
    {Opcode::MOD,  "MOD"},  {Opcode::LOAD, "LOAD"}, {Opcode::STORE,"STORE"},
    {Opcode::PUSH, "PUSH"}, {Opcode::POP,  "POP"},  {Opcode::LEA,  "LEA"},
    {Opcode::JMP,  "JMP"},  {Opcode::CMP,  "CMP"},  {Opcode::JE,   "JE"},
    {Opcode::JNE,  "JNE"},  {Opcode::JLT,  "JLT"},  {Opcode::JGT,  "JGT"},
    {Opcode::JLE,  "JLE"},  {Opcode::JGE,  "JGE"},  {Opcode::CALL, "CALL"},
    {Opcode::RET,  "RET"},  {Opcode::INT,  "INT"},  {Opcode::HLT,  "HLT"},
    {Opcode::IRET, "IRET"}, {Opcode::LEA,  "LEA"},  {Opcode::INVALID,"???"},
}};

inline std::string_view opcode_name(Opcode op) noexcept {
    for (auto& e : OPCODE_TABLE)
        if (e.op == op) return e.name;
    return "???";
}

} // namespace TA64
