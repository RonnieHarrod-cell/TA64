// ============================================================
//  ta-monitor — TA64 Interactive Debugger
//  Commands: run, step, break, regs, mem, dis, quit
// ============================================================
#include "ta64_isa.hpp"
#include "memory.hpp"
#include "cpu.hpp"
#include "loader.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>

using namespace TA64;

static void help() {
    std::cout <<
        "TA64 Monitor Commands:\n"
        "  r / run              Run until HLT or breakpoint\n"
        "  s / step [n]         Step n instructions (default 1)\n"
        "  b / break <hex>      Set breakpoint at address\n"
        "  d / del   <hex>      Delete breakpoint\n"
        "  regs                 Dump registers\n"
        "  mem  <hex> [len]     Dump memory (default 64 bytes)\n"
        "  dis  <hex> [n]       Disassemble n instructions (default 8)\n"
        "  pc   <hex>           Set PC\n"
        "  sp   <hex>           Set SP\n"
        "  setreg R<n> <hex>    Set register value\n"
        "  reset                Reset CPU\n"
        "  q / quit             Exit\n";
}

int main(int argc, char* argv[]) {
    std::cout << "TA64 Monitor v1.0  (type 'help' for commands)\n";

    Memory mem;
    CPU    cpu(mem);

    // Install a syscall handler that prints to stdout
    cpu.set_syscall_handler([&](CPU& c) {
        uint64_t num = c.reg(0);
        switch (num) {
        case Syscall::SYS_EXIT:
            c.fl() |= Flags::HALT;
            std::cout << "\n[monitor] Program exited (SYS_EXIT)\n";
            break;
        case Syscall::SYS_WRITE: {
            uint64_t addr = c.reg(2);
            uint64_t len  = c.reg(3);
            for (uint64_t i = 0; i < len; ++i)
                std::cout << (char)mem.read8(addr + i);
            std::cout.flush();
            c.reg(0) = len;
            break;
        }
        default:
            std::cerr << "[monitor] Unhandled syscall " << num << "\n";
        }
    });

    // Optionally load binary from args
    if (argc >= 2) {
        try {
            BinaryLoader::load(argv[1], mem, cpu);
            std::cout << "[monitor] Loaded: " << argv[1] << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[monitor] Load error: " << e.what() << "\n";
        }
    }

    std::string line;
    while (true) {
        printf("(ta-mon) ");
        std::fflush(stdout);
        if (!std::getline(std::cin, line)) break;

        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd.empty()) continue;

        // ── load ──────────────────────────────────────────
        if (cmd == "load") {
            std::string path; ss >> path;
            if (path.empty()) { std::cerr << "Usage: load <file.taexe>\n"; continue; }
            try {
                BinaryLoader::load(path, mem, cpu);
                std::cout << "Loaded: " << path << "\n";
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << "\n";
            }
        }
        // ── help ──────────────────────────────────────────
        else if (cmd == "help" || cmd == "?") {
            help();
        }
        // ── run ───────────────────────────────────────────
        else if (cmd == "r" || cmd == "run") {
            cpu.fl() &= ~Flags::HALT;
            cpu.set_trace(false);
            try {
                cpu.run();
                std::cout << "\n[monitor] Halted. Cycles: " << cpu.cycles() << "\n";
            } catch (const std::exception& e) {
                std::cerr << "[CPU exception] " << e.what() << "\n";
            }
        }
        // ── step ──────────────────────────────────────────
        else if (cmd == "s" || cmd == "step") {
            int n = 1;
            ss >> n;
            cpu.fl() &= ~Flags::HALT;
            cpu.set_trace(true);
            try {
                for (int i = 0; i < n && !(cpu.fl() & Flags::HALT); ++i)
                    cpu.step();
            } catch (const std::exception& e) {
                std::cerr << "[CPU exception] " << e.what() << "\n";
            }
            cpu.set_trace(false);
            // Print current instruction
            uint32_t raw = mem.read32(cpu.pc());
            printf("[next] %s\n", disassemble(cpu.pc(), raw).c_str());
        }
        // ── break ─────────────────────────────────────────
        else if (cmd == "b" || cmd == "break") {
            std::string hexs; ss >> hexs;
            uint64_t addr = std::stoull(hexs, nullptr, 16);
            cpu.add_breakpoint(addr);
            printf("[monitor] Breakpoint set at 0x%08llX\n", (unsigned long long)addr);
        }
        // ── del ───────────────────────────────────────────
        else if (cmd == "d" || cmd == "del") {
            std::string hexs; ss >> hexs;
            uint64_t addr = std::stoull(hexs, nullptr, 16);
            cpu.remove_breakpoint(addr);
            printf("[monitor] Breakpoint removed at 0x%08llX\n", (unsigned long long)addr);
        }
        // ── regs ──────────────────────────────────────────
        else if (cmd == "regs") {
            cpu.dump_registers();
        }
        // ── mem ───────────────────────────────────────────
        else if (cmd == "mem") {
            std::string hexs; ss >> hexs;
            uint64_t addr = std::stoull(hexs, nullptr, 16);
            uint64_t len  = 64;
            ss >> len;
            cpu.dump_memory(addr, len);
        }
        // ── dis ───────────────────────────────────────────
        else if (cmd == "dis") {
            std::string hexs; ss >> hexs;
            uint64_t addr;
            if (hexs.empty()) addr = cpu.pc();
            else              addr = std::stoull(hexs, nullptr, 16);
            int n = 8;
            ss >> n;
            for (int i = 0; i < n; ++i) {
                uint64_t a = addr + i*4;
                if (a + 3 >= Memory::SIZE) break;
                uint32_t raw = mem.read32(a);
                printf("  %c %s\n",
                       (a == cpu.pc()) ? '>' : ' ',
                       disassemble(a, raw).c_str());
            }
        }
        // ── pc ────────────────────────────────────────────
        else if (cmd == "pc") {
            std::string hexs; ss >> hexs;
            cpu.pc() = std::stoull(hexs, nullptr, 16);
            printf("[monitor] PC = 0x%08llX\n", (unsigned long long)cpu.pc());
        }
        // ── sp ────────────────────────────────────────────
        else if (cmd == "sp") {
            std::string hexs; ss >> hexs;
            cpu.sp() = std::stoull(hexs, nullptr, 16);
            printf("[monitor] SP = 0x%08llX\n", (unsigned long long)cpu.sp());
        }
        // ── setreg ────────────────────────────────────────
        else if (cmd == "setreg") {
            std::string rname, hexs;
            ss >> rname >> hexs;
            // Parse R<n>
            if (rname.size() >= 2 && (rname[0]=='R' || rname[0]=='r')) {
                int n = std::stoi(rname.substr(1));
                if (n >= 0 && n <= 15) {
                    cpu.reg(n) = std::stoull(hexs, nullptr, 16);
                    printf("[monitor] R%d = 0x%llX\n", n, (unsigned long long)cpu.reg(n));
                }
            }
        }
        // ── trace ─────────────────────────────────────────
        else if (cmd == "trace") {
            std::string on; ss >> on;
            cpu.set_trace(on != "off");
            std::cout << "[monitor] Trace " << (on != "off" ? "ON" : "OFF") << "\n";
        }
        // ── reset ─────────────────────────────────────────
        else if (cmd == "reset") {
            cpu.reset();
            std::cout << "[monitor] CPU reset.\n";
        }
        // ── quit ──────────────────────────────────────────
        else if (cmd == "q" || cmd == "quit" || cmd == "exit") {
            break;
        }
        // ── snapshot ──────────────────────────────────────
        else if (cmd == "snap") {
            auto s = cpu.snapshot();
            printf("[snapshot] PC=%llX SP=%llX FL=%llX cycles=%llu\n",
                   (unsigned long long)s.pc,
                   (unsigned long long)s.sp,
                   (unsigned long long)s.fl,
                   (unsigned long long)s.cycles);
        }
        else {
            std::cerr << "Unknown command: " << cmd << "  (type 'help')\n";
        }
    }

    std::cout << "[monitor] Bye.\n";
    return 0;
}
