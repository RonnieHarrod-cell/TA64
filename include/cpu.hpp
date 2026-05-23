#pragma once
// ============================================================
//  TA64 CPU
//  Implements the full fetch-decode-execute loop.
//  Supports debug tracing, single-step, and breakpoints.
// ============================================================
#include "ta64_isa.hpp"
#include "memory.hpp"
#include <array>
#include <functional>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace TA64 {

// ─── CPU state snapshot (for debug / checkpointing) ────────
struct CPUState {
    std::array<uint64_t, 16> regs{};
    uint64_t sp{}, pc{}, fl{};
    uint64_t cycles{};
};

// ─── Syscall handler signature ─────────────────────────────
class CPU; // forward
using SyscallHandler = std::function<void(CPU&)>;

// ─── Disassembler helper ────────────────────────────────────
inline std::string disassemble(uint64_t addr, uint32_t raw) {
    auto ins  = Instruction::decode(raw);
    auto name = opcode_name(ins.opcode);
    char buf[128];

    auto reg_name = [](uint8_t r) -> std::string {
        if (r <= 15) return "R" + std::to_string(r);
        if (r == SP_IDX) return "SP";
        if (r == PC_IDX) return "PC";
        if (r == FL_IDX) return "FL";
        return "?";
    };

    switch (ins.opcode) {
    case Opcode::NOP: case Opcode::RET: case Opcode::HLT: case Opcode::IRET:
        snprintf(buf, sizeof(buf), "%04llX: %-6s", (unsigned long long)addr, name.data());
        break;
    case Opcode::MOV:
        if (ins.arg1_is_imm())
            snprintf(buf, sizeof(buf), "%04llX: %-6s R%d, #%d",
                (unsigned long long)addr, name.data(), ins.reg1(), ins.arg2);
        else
            snprintf(buf, sizeof(buf), "%04llX: %-6s %s, %s",
                (unsigned long long)addr, name.data(),
                reg_name(ins.reg1()).c_str(), reg_name(ins.reg2()).c_str());
        break;
    case Opcode::MOVI:
        snprintf(buf, sizeof(buf), "%04llX: %-6s R%d, #%d",
            (unsigned long long)addr, name.data(), ins.reg1(), (int)ins.imm16());
        break;
    case Opcode::INC: case Opcode::DEC: case Opcode::NOT:
    case Opcode::PUSH: case Opcode::POP:
        snprintf(buf, sizeof(buf), "%04llX: %-6s %s",
            (unsigned long long)addr, name.data(), reg_name(ins.arg1 & 0x0F).c_str());
        break;
    case Opcode::JMP: case Opcode::JE: case Opcode::JNE:
    case Opcode::JLT: case Opcode::JGT: case Opcode::JLE:
    case Opcode::JGE: case Opcode::CALL:
        if (ins.arg1 & IMM_FLAG)
            snprintf(buf, sizeof(buf), "%04llX: %-6s #%+d",
                (unsigned long long)addr, name.data(), ins.simm16());
        else
            snprintf(buf, sizeof(buf), "%04llX: %-6s %s",
                (unsigned long long)addr, name.data(), reg_name(ins.arg1 & 0x0F).c_str());
        break;
    case Opcode::INT:
        snprintf(buf, sizeof(buf), "%04llX: %-6s 0x%02X",
            (unsigned long long)addr, name.data(), ins.arg1);
        break;
    default:
        snprintf(buf, sizeof(buf), "%04llX: %-6s R%d, R%d, R%d",
            (unsigned long long)addr, name.data(),
            ins.reg1(), ins.reg2(), ins.reg3());
        break;
    }
    return buf;
}

// ─── CPU ────────────────────────────────────────────────────
class CPU {
public:
    // ── Construction ────────────────────────────────────────
    explicit CPU(Memory& mem) : mem_(mem) {
        reset();
        // Default syscall handler
        syscall_handler_ = [](CPU& cpu) {
            uint64_t num = cpu.reg(0);
            switch (num) {
            case Syscall::SYS_EXIT:
                cpu.fl() |= Flags::HALT;
                break;
            case Syscall::SYS_WRITE: {
                // R1 = fd, R2 = buf address, R3 = length
                uint64_t addr = cpu.reg(2);
                uint64_t len  = cpu.reg(3);
                for (uint64_t i = 0; i < len; ++i)
                    std::cout << (char)cpu.mem_.read8(addr + i);
                std::cout.flush();
                cpu.reg(0) = len; // return bytes written
                break;
            }
            case Syscall::SYS_GETPID:
                cpu.reg(0) = 1; // PID always 1 (single process)
                break;
            default:
                std::cerr << "[SYSCALL] Unknown syscall " << num << "\n";
                break;
            }
        };
    }

    // ── Register access ────────────────────────────────────
    uint64_t& reg(uint8_t idx) {
        if (idx < 16) return gpr_[idx];
        if (idx == SP_IDX) return sp_;
        if (idx == PC_IDX) return pc_;
        if (idx == FL_IDX) return fl_;
        throw std::out_of_range("Bad register index");
    }
    uint64_t reg(uint8_t idx) const {
        if (idx < 16) return gpr_[idx];
        if (idx == SP_IDX) return sp_;
        if (idx == PC_IDX) return pc_;
        if (idx == FL_IDX) return fl_;
        throw std::out_of_range("Bad register index");
    }
    uint64_t& fl()  noexcept { return fl_; }
    uint64_t& sp()  noexcept { return sp_; }
    uint64_t& pc()  noexcept { return pc_; }

    // ── Reset / initialise ──────────────────────────────────
    void reset() {
        gpr_.fill(0);
        sp_     = DEFAULT_STACK_TOP;
        pc_     = DEFAULT_LOAD_ADDR;
        fl_     = 0;
        cycles_ = 0;
    }

    // ── Debug config ────────────────────────────────────────
    void set_trace(bool on)           noexcept { trace_       = on; }
    void set_single_step(bool on)     noexcept { single_step_ = on; }
    void add_breakpoint(uint64_t addr)          { breakpoints_.insert(addr); }
    void remove_breakpoint(uint64_t addr)       { breakpoints_.erase(addr); }
    void set_syscall_handler(SyscallHandler h)  { syscall_handler_ = std::move(h); }

    uint64_t cycles() const noexcept { return cycles_; }

    // ── Main execution loop ─────────────────────────────────
    void run(uint64_t max_cycles = UINT64_MAX) {
        while (!(fl_ & Flags::HALT) && cycles_ < max_cycles) {
            if (breakpoints_.count(pc_)) {
                std::cout << "[BREAK] at " << std::hex << pc_ << std::dec << "\n";
                dump_registers();
                single_step_ = true;
            }
            step();
            if (single_step_) {
                std::cout << "[STEP] Press Enter...\n";
                std::cin.get();
            }
        }
    }

    // ── Single step ─────────────────────────────────────────
    void step() {
        uint32_t raw = mem_.read32(pc_);
        auto ins = Instruction::decode(raw);

        if (trace_) {
            std::cout << "  " << disassemble(pc_, raw)
                      << "  [SP=" << std::hex << sp_
                      << " FL=" << fl_ << std::dec << "]\n";
        }

        uint64_t next_pc = pc_ + 4;
        execute(ins, next_pc);
        pc_ = next_pc;
        ++cycles_;
    }

    // ── Register & memory dumps ─────────────────────────────
    void dump_registers() const {
        std::cout << "=== Register Dump ===\n";
        for (int i = 0; i < 16; i += 4) {
            for (int j = 0; j < 4; ++j)
                printf("  R%-2d = %016llX", i+j, (unsigned long long)gpr_[i+j]);
            printf("\n");
        }
        printf("  SP  = %016llX  PC  = %016llX  FL  = %016llX\n",
               (unsigned long long)sp_,
               (unsigned long long)pc_,
               (unsigned long long)fl_);
        printf("  Cycles: %llu\n", (unsigned long long)cycles_);
    }

    void dump_memory(uint64_t start, uint64_t len) const {
        std::cout << mem_.hex_dump(start, len);
    }

    CPUState snapshot() const {
        CPUState s;
        s.regs = gpr_;
        s.sp = sp_; s.pc = pc_; s.fl = fl_;
        s.cycles = cycles_;
        return s;
    }

    // Expose memory for syscall handler etc.
    Memory& memory() noexcept { return mem_; }

private:
    Memory& mem_;
    std::array<uint64_t, 16> gpr_{};
    uint64_t sp_{}, pc_{}, fl_{};
    uint64_t cycles_{};
    bool trace_       = false;
    bool single_step_ = false;
    std::set<uint64_t>   breakpoints_;
    SyscallHandler       syscall_handler_;

    // ── Flag helpers ────────────────────────────────────────
    void set_flag(uint64_t mask, bool v) noexcept {
        v ? (fl_ |= mask) : (fl_ &= ~mask);
    }
    void update_nz(uint64_t v) noexcept {
        set_flag(Flags::ZERO,     v == 0);
        set_flag(Flags::NEGATIVE, v >> 63);
    }

    // ── Stack helpers ────────────────────────────────────────
    void push64(uint64_t val) {
        sp_ -= 8;
        mem_.write64(sp_, val);
    }
    uint64_t pop64() {
        uint64_t v = mem_.read64(sp_);
        sp_ += 8;
        return v;
    }

    // ── Execute one decoded instruction ─────────────────────
    void execute(const Instruction& ins, uint64_t& next_pc) {
        switch (ins.opcode) {
        // ── NOP ──────────────────────────────────────────────
        case Opcode::NOP: break;

        // ── MOV Rdst, Rsrc | MOV Rdst, #imm8 ────────────────
        case Opcode::MOV:
            if (ins.arg1_is_imm())
                reg(ins.reg1()) = ins.arg2;
            else
                reg(ins.reg1()) = reg(ins.reg2());
            update_nz(reg(ins.reg1()));
            break;

        // ── MOVI Rdst, #imm16 ────────────────────────────────
        case Opcode::MOVI:
            reg(ins.reg1()) = ins.imm16();
            update_nz(reg(ins.reg1()));
            break;

        // ── Arithmetic ───────────────────────────────────────
        case Opcode::ADD: {
            uint64_t a = reg(ins.reg1()), b = reg(ins.reg2());
            uint64_t r = a + b;
            reg(ins.reg1()) = r;
            set_flag(Flags::CARRY,    r < a);
            set_flag(Flags::OVERFLOW, (~(a ^ b) & (a ^ r)) >> 63);
            update_nz(r);
            break;
        }
        case Opcode::SUB: {
            uint64_t a = reg(ins.reg1()), b = reg(ins.reg2());
            uint64_t r = a - b;
            reg(ins.reg1()) = r;
            set_flag(Flags::CARRY,    a < b);
            set_flag(Flags::OVERFLOW, ((a ^ b) & (a ^ r)) >> 63);
            update_nz(r);
            break;
        }
        case Opcode::MUL:
            reg(ins.reg1()) = reg(ins.reg1()) * reg(ins.reg2());
            update_nz(reg(ins.reg1()));
            break;
        case Opcode::DIV:
            if (reg(ins.reg2()) == 0) throw std::runtime_error("Division by zero");
            reg(ins.reg1()) = reg(ins.reg1()) / reg(ins.reg2());
            update_nz(reg(ins.reg1()));
            break;
        case Opcode::MOD:
            if (reg(ins.reg2()) == 0) throw std::runtime_error("Modulo by zero");
            reg(ins.reg1()) = reg(ins.reg1()) % reg(ins.reg2());
            update_nz(reg(ins.reg1()));
            break;
        case Opcode::INC:
            ++reg(ins.arg1 & 0x0F);
            update_nz(reg(ins.arg1 & 0x0F));
            break;
        case Opcode::DEC:
            --reg(ins.arg1 & 0x0F);
            update_nz(reg(ins.arg1 & 0x0F));
            break;
        case Opcode::AND:
            reg(ins.reg1()) &= reg(ins.reg2());
            update_nz(reg(ins.reg1()));
            break;
        case Opcode::OR:
            reg(ins.reg1()) |= reg(ins.reg2());
            update_nz(reg(ins.reg1()));
            break;
        case Opcode::XOR:
            reg(ins.reg1()) ^= reg(ins.reg2());
            update_nz(reg(ins.reg1()));
            break;
        case Opcode::NOT:
            reg(ins.arg1 & 0x0F) = ~reg(ins.arg1 & 0x0F);
            update_nz(reg(ins.arg1 & 0x0F));
            break;
        case Opcode::SHL:
            reg(ins.reg1()) <<= (reg(ins.reg2()) & 63);
            update_nz(reg(ins.reg1()));
            break;
        case Opcode::SHR:
            reg(ins.reg1()) >>= (reg(ins.reg2()) & 63);
            update_nz(reg(ins.reg1()));
            break;

        // ── Memory ───────────────────────────────────────────
        case Opcode::LOAD: {
            int16_t off = static_cast<int16_t>(static_cast<int8_t>(ins.arg3));
            reg(ins.reg1()) = mem_.read64(reg(ins.reg2()) + off);
            break;
        }
        case Opcode::STORE: {
            int16_t off = static_cast<int16_t>(static_cast<int8_t>(ins.arg3));
            mem_.write64(reg(ins.reg2()) + off, reg(ins.reg1()));
            break;
        }
        case Opcode::PUSH:
            push64(reg(ins.arg1 & 0x0F));
            break;
        case Opcode::POP:
            reg(ins.arg1 & 0x0F) = pop64();
            break;
        case Opcode::LEA:
            reg(ins.reg1()) = DEFAULT_LOAD_ADDR + ins.imm16();
            break;

        // ── CMP ──────────────────────────────────────────────
        case Opcode::CMP: {
            uint64_t a = reg(ins.reg1()), b = reg(ins.reg2());
            uint64_t r = a - b;
            set_flag(Flags::ZERO,     r == 0);
            set_flag(Flags::NEGATIVE, r >> 63);
            set_flag(Flags::CARRY,    a < b);
            break;
        }

        // ── Jumps ────────────────────────────────────────────
        case Opcode::JMP:
            if (ins.arg1 & IMM_FLAG)
                next_pc = pc_ + 4 + static_cast<int64_t>(ins.simm16()) * 4;
            else
                next_pc = reg(ins.arg1 & 0x0F);
            break;
        case Opcode::JE:
            if (fl_ & Flags::ZERO)
                next_pc = pc_ + 4 + static_cast<int64_t>(ins.simm16()) * 4;
            break;
        case Opcode::JNE:
            if (!(fl_ & Flags::ZERO))
                next_pc = pc_ + 4 + static_cast<int64_t>(ins.simm16()) * 4;
            break;
        case Opcode::JLT:
            if (fl_ & Flags::NEGATIVE)
                next_pc = pc_ + 4 + static_cast<int64_t>(ins.simm16()) * 4;
            break;
        case Opcode::JGT:
            if (!(fl_ & Flags::ZERO) && !(fl_ & Flags::NEGATIVE))
                next_pc = pc_ + 4 + static_cast<int64_t>(ins.simm16()) * 4;
            break;
        case Opcode::JLE:
            if ((fl_ & Flags::ZERO) || (fl_ & Flags::NEGATIVE))
                next_pc = pc_ + 4 + static_cast<int64_t>(ins.simm16()) * 4;
            break;
        case Opcode::JGE:
            if (!(fl_ & Flags::NEGATIVE))
                next_pc = pc_ + 4 + static_cast<int64_t>(ins.simm16()) * 4;
            break;

        // ── CALL / RET ───────────────────────────────────────
        case Opcode::CALL:
            push64(next_pc); // push return address
            if (ins.arg1 & IMM_FLAG)
                next_pc = pc_ + 4 + static_cast<int64_t>(ins.simm16()) * 4;
            else
                next_pc = reg(ins.arg1 & 0x0F);
            break;
        case Opcode::RET:
            next_pc = pop64();
            break;

        // ── System ───────────────────────────────────────────
        case Opcode::INT:
            if (ins.arg1 == 0x80)
                syscall_handler_(*this);
            else
                std::cerr << "[INT] Unhandled vector 0x" << std::hex << (int)ins.arg1 << "\n";
            break;
        case Opcode::HLT:
            fl_ |= Flags::HALT;
            break;
        case Opcode::IRET:
            next_pc = pop64(); // restore PC from stack
            fl_     = pop64(); // restore flags
            break;

        default:
            throw std::runtime_error("Invalid opcode 0x" +
                std::to_string(static_cast<int>(ins.opcode)));
        }
    }
};

} // namespace TA64
