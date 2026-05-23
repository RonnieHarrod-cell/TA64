// ============================================================
//  ta-objdump — TA64 Object File Inspector
//
//  Shows: TAEXE header details, section map, symbol-like
//  patterns (labels inferred from CALL targets), full
//  annotated disassembly with hex bytes.
//
//  Usage:
//    ta-objdump <file.taexe>            full report
//    ta-objdump <file.taexe> --headers  header only
//    ta-objdump <file.taexe> --text     disassembly only
//    ta-objdump <file.taexe> --data     data dump only
// ============================================================
#include "ta64_isa.hpp"
#include "loader.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstring>

using namespace TA64;

// ─── Infer call targets as pseudo-symbols ────────────────────
static std::map<uint64_t, std::string> infer_symbols(
    const std::vector<uint8_t>& text, uint64_t base)
{
    std::map<uint64_t, std::string> syms;
    syms[base] = "_start";
    int call_n = 0;

    for (size_t i = 0; i + 3 < text.size(); i += 4) {
        uint32_t raw = text[i] | ((uint32_t)text[i+1]<<8)
                     | ((uint32_t)text[i+2]<<16) | ((uint32_t)text[i+3]<<24);
        auto ins = Instruction::decode(raw);
        if (ins.opcode == Opcode::CALL && (ins.arg1 & IMM_FLAG)) {
            int64_t delta  = ins.simm16();
            uint64_t target = (base + i + 4) + delta * 4;
            if (syms.find(target) == syms.end()) {
                syms[target] = "fn_" + std::to_string(call_n++);
            }
        }
    }
    return syms;
}

// ─── Print header section ────────────────────────────────────
static void print_header(const TAEXEHeader& hdr) {
    printf("╔══════════════════════════════════════╗\n");
    printf("║         TAEXE File Header            ║\n");
    printf("╚══════════════════════════════════════╝\n");
    printf("  Magic        : %c%c%c%c\n",
           hdr.magic[0], hdr.magic[1], hdr.magic[2], hdr.magic[3]);
    printf("  Version      : %u\n",    hdr.version);
    printf("  Load address : 0x%016llX\n", (unsigned long long)hdr.load_address);
    printf("  Entry point  : 0x%016llX\n", (unsigned long long)hdr.entry_point);
    printf("\n  Section Map:\n");
    printf("  ┌─────────┬──────────────────┬──────────────────┐\n");
    printf("  │ Section │ File offset      │ Size             │\n");
    printf("  ├─────────┼──────────────────┼──────────────────┤\n");
    printf("  │ .text   │ 0x%016llX │ %-16llu │\n",
           (unsigned long long)hdr.text_offset, (unsigned long long)hdr.text_size);
    printf("  │ .data   │ 0x%016llX │ %-16llu │\n",
           (unsigned long long)hdr.data_offset, (unsigned long long)hdr.data_size);
    printf("  │ .bss    │ (not stored)     │ %-16llu │\n",
           (unsigned long long)hdr.bss_size);
    printf("  └─────────┴──────────────────┴──────────────────┘\n");
    uint64_t total = sizeof(TAEXEHeader) + hdr.text_size + hdr.data_size;
    printf("  Total file size (excl. bss): %llu bytes\n\n", (unsigned long long)total);
}

// ─── Disassemble .text ───────────────────────────────────────
static void print_text(const std::vector<uint8_t>& text, uint64_t base,
                       uint64_t entry,
                       const std::map<uint64_t,std::string>& syms)
{
    printf("╔══════════════════════════════════════╗\n");
    printf("║         Disassembly of .text         ║\n");
    printf("╚══════════════════════════════════════╝\n");
    printf("  %zu bytes  load=0x%llX\n\n", text.size(), (unsigned long long)base);

    for (size_t i = 0; i + 3 < text.size(); i += 4) {
        uint64_t addr = base + i;

        // Print symbol label if any
        auto it = syms.find(addr);
        if (it != syms.end())
            printf("\n<%s>:\n", it->second.c_str());

        uint32_t raw = text[i] | ((uint32_t)text[i+1]<<8)
                     | ((uint32_t)text[i+2]<<16) | ((uint32_t)text[i+3]<<24);

        std::string dis = disassemble(addr, raw);

        // Annotations
        std::string annotation;
        auto ins = Instruction::decode(raw);
        if (ins.opcode == Opcode::CALL && (ins.arg1 & IMM_FLAG)) {
            int64_t delta = ins.simm16();
            uint64_t tgt  = addr + 4 + delta * 4;
            auto sit = syms.find(tgt);
            annotation = " → " + (sit != syms.end() ? sit->second
                                                      : "0x" + std::to_string(tgt));
        }
        if (addr == entry)
            annotation += "  ◄ ENTRY";

        printf("  %08llX:  %02X %02X %02X %02X   %-32s%s\n",
               (unsigned long long)addr,
               text[i], text[i+1], text[i+2], text[i+3],
               dis.c_str(), annotation.c_str());
    }
    printf("\n");
}

// ─── Hex+ASCII dump of .data ─────────────────────────────────
static void print_data(const std::vector<uint8_t>& data, uint64_t base) {
    if (data.empty()) { printf("  (empty)\n\n"); return; }
    printf("╔══════════════════════════════════════╗\n");
    printf("║         Contents of .data            ║\n");
    printf("╚══════════════════════════════════════╝\n");
    printf("  %zu bytes  VA=0x%llX\n\n", data.size(), (unsigned long long)base);

    for (size_t i = 0; i < data.size(); i += 16) {
        printf("  %08llX  ", (unsigned long long)(base + i));
        for (size_t j = 0; j < 16; ++j) {
            if (i+j < data.size()) printf("%02X ", data[i+j]);
            else                   printf("   ");
            if (j == 7) printf(" ");
        }
        printf(" |");
        for (size_t j = 0; j < 16 && i+j < data.size(); ++j) {
            char c = (char)data[i+j];
            printf("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
        }
        printf("|\n");
    }
    printf("\n");
}

// ─── Statistics ──────────────────────────────────────────────
static void print_stats(const std::vector<uint8_t>& text, uint64_t base) {
    std::map<Opcode,int> freq;
    int branches = 0, calls = 0, memory_ops = 0, syscalls = 0;

    for (size_t i = 0; i + 3 < text.size(); i += 4) {
        uint32_t raw = text[i] | ((uint32_t)text[i+1]<<8)
                     | ((uint32_t)text[i+2]<<16) | ((uint32_t)text[i+3]<<24);
        auto ins = Instruction::decode(raw);
        freq[ins.opcode]++;
        switch (ins.opcode) {
        case Opcode::JMP: case Opcode::JE: case Opcode::JNE:
        case Opcode::JLT: case Opcode::JGT: case Opcode::JLE: case Opcode::JGE:
            branches++;  break;
        case Opcode::CALL:  calls++;       break;
        case Opcode::LOAD: case Opcode::STORE: case Opcode::PUSH: case Opcode::POP:
            memory_ops++;  break;
        case Opcode::INT:   syscalls++;    break;
        default: break;
        }
    }

    printf("╔══════════════════════════════════════╗\n");
    printf("║         Code Statistics              ║\n");
    printf("╚══════════════════════════════════════╝\n");
    size_t total = text.size() / 4;
    printf("  Total instructions : %zu\n", total);
    printf("  Branch instructions: %d  (%.1f%%)\n", branches, 100.0*branches/total);
    printf("  Function calls     : %d\n", calls);
    printf("  Memory ops         : %d  (%.1f%%)\n", memory_ops, 100.0*memory_ops/total);
    printf("  Syscalls           : %d\n\n", syscalls);

    printf("  Top opcodes:\n");
    // Sort by frequency
    std::vector<std::pair<int,Opcode>> sorted;
    for (auto& [op, cnt] : freq) sorted.push_back({cnt, op});
    std::sort(sorted.rbegin(), sorted.rend());
    int shown = 0;
    for (auto& [cnt, op] : sorted) {
        if (shown++ >= 8) break;
        printf("    %-6s  %3d  ", opcode_name(op).data(), cnt);
        int bar = cnt * 30 / sorted[0].first;
        for (int i = 0; i < bar; ++i) printf("█");
        printf("\n");
    }
    printf("\n");
}

// ─── Main ────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ta-objdump <file.taexe> [--headers|--text|--data|--stats]\n";
        return 1;
    }

    std::string path = argv[1];
    bool show_headers = true, show_text = true, show_data = true, show_stats = true;

    for (int i = 2; i < argc; ++i) {
        std::string f = argv[i];
        if (f == "--headers") { show_text = show_data = show_stats = false; }
        else if (f == "--text")    { show_headers = show_data = show_stats = false; }
        else if (f == "--data")    { show_headers = show_text = show_stats = false; }
        else if (f == "--stats")   { show_headers = show_text = show_data = false; }
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "Cannot open: " << path << "\n"; return 1; }

    TAEXEHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f || hdr.magic[0]!='T'||hdr.magic[1]!='A'||
              hdr.magic[2]!='6'||hdr.magic[3]!='4') {
        std::cerr << "Not a valid TAEXE file.\n"; return 1;
    }

    uint64_t load = hdr.load_address ? hdr.load_address : DEFAULT_LOAD_ADDR;

    std::vector<uint8_t> text, data;
    if (hdr.text_size) {
        text.resize(hdr.text_size);
        f.seekg((std::streamoff)hdr.text_offset);
        f.read(reinterpret_cast<char*>(text.data()), hdr.text_size);
    }
    if (hdr.data_size) {
        data.resize(hdr.data_size);
        f.seekg((std::streamoff)hdr.data_offset);
        f.read(reinterpret_cast<char*>(data.data()), hdr.data_size);
    }

    printf("\n  TA64 Object Dump: %s\n\n", path.c_str());
    auto syms = infer_symbols(text, load);

    if (show_headers) print_header(hdr);
    if (show_text)    print_text(text, load, hdr.entry_point, syms);
    if (show_data)    print_data(data, load + hdr.text_size);
    if (show_stats && !text.empty()) print_stats(text, load);

    return 0;
}
