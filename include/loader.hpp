#pragma once
// ============================================================
//  TA64 Binary Loader
//  Reads TAEXE headers, validates magic, loads .text + .data,
//  zero-fills .bss, and sets the CPU's initial PC / SP.
// ============================================================
#include "ta64_isa.hpp"
#include "memory.hpp"
#include "cpu.hpp"
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace TA64 {

class BinaryLoader {
public:
    // ── Load a TAEXE file into memory, configure CPU ────────
    static void load(const std::string& path, Memory& mem, CPU& cpu) {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("Cannot open binary: " + path);

        TAEXEHeader hdr{};
        f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        if (!f)
            throw std::runtime_error("File too small for TAEXE header");

        // Validate magic
        if (hdr.magic[0] != 'T' || hdr.magic[1] != 'A' ||
            hdr.magic[2] != '6' || hdr.magic[3] != '4')
            throw std::runtime_error("Not a TAEXE file: bad magic in " + path);

        // Sanity checks
        if (hdr.load_address + hdr.text_size + hdr.data_size + hdr.bss_size > Memory::SIZE)
            throw std::runtime_error("Binary exceeds emulated memory");

        uint64_t load = hdr.load_address ? hdr.load_address : DEFAULT_LOAD_ADDR;

        // Load .text
        if (hdr.text_size > 0) {
            f.seekg(static_cast<std::streamoff>(hdr.text_offset));
            std::vector<uint8_t> text(hdr.text_size);
            f.read(reinterpret_cast<char*>(text.data()), hdr.text_size);
            mem.load(load, text);
        }

        // Load .data (immediately after .text in memory)
        if (hdr.data_size > 0) {
            uint64_t data_va = load + hdr.text_size;
            f.seekg(static_cast<std::streamoff>(hdr.data_offset));
            std::vector<uint8_t> data(hdr.data_size);
            f.read(reinterpret_cast<char*>(data.data()), hdr.data_size);
            mem.load(data_va, data);
        }

        // Zero-fill .bss
        if (hdr.bss_size > 0) {
            uint64_t bss_va = load + hdr.text_size + hdr.data_size;
            for (uint64_t i = 0; i < hdr.bss_size; ++i)
                mem.write8(bss_va + i, 0);
        }

        // Configure CPU
        cpu.pc() = hdr.entry_point ? hdr.entry_point : load;
        cpu.sp() = DEFAULT_STACK_TOP;

        printf("[Loader] Loaded %s\n", path.c_str());
        printf("  text   @ 0x%08llX  size=%llu bytes\n",
               (unsigned long long)load, (unsigned long long)hdr.text_size);
        if (hdr.data_size)
            printf("  data   @ 0x%08llX  size=%llu bytes\n",
                   (unsigned long long)(load + hdr.text_size),
                   (unsigned long long)hdr.data_size);
        if (hdr.bss_size)
            printf("  bss    @ 0x%08llX  size=%llu bytes\n",
                   (unsigned long long)(load + hdr.text_size + hdr.data_size),
                   (unsigned long long)hdr.bss_size);
        printf("  entry  @ 0x%08llX\n", (unsigned long long)cpu.pc());
    }

    // ── Convenience: save raw code blob as TAEXE ────────────
    static void write_taexe(const std::string& path,
                             const std::vector<uint8_t>& text,
                             const std::vector<uint8_t>& data = {},
                             uint64_t entry = DEFAULT_LOAD_ADDR,
                             uint64_t load  = DEFAULT_LOAD_ADDR) {
        TAEXEHeader hdr{};
        hdr.version      = 1;
        hdr.entry_point  = entry;
        hdr.load_address = load;
        hdr.text_offset  = sizeof(TAEXEHeader);
        hdr.text_size    = text.size();
        hdr.data_offset  = sizeof(TAEXEHeader) + text.size();
        hdr.data_size    = data.size();
        hdr.bss_size     = 0;

        std::ofstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("Cannot write: " + path);
        f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        f.write(reinterpret_cast<const char*>(text.data()), text.size());
        if (!data.empty())
            f.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
};

} // namespace TA64
