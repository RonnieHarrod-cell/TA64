// ============================================================
//  ta-dasm — TA64 Disassembler
//  Reads a TAEXE binary and prints the disassembly.
// ============================================================
#include "ta64_isa.hpp"
#include "loader.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

using namespace TA64;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ta-dasm <binary.taexe> [-a (show all sections)]\n";
        return 1;
    }

    bool show_data = false;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-a") show_data = true;
        else path = a;
    }

    // Read TAEXE
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "Cannot open: " << path << "\n"; return 1; }

    TAEXEHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (hdr.magic[0]!='T'||hdr.magic[1]!='A'||
        hdr.magic[2]!='6'||hdr.magic[3]!='4') {
        std::cerr << "Not a TAEXE file\n"; return 1;
    }

    printf("=== TA64 Disassembly: %s ===\n", path.c_str());
    printf("Entry: 0x%08llX  Load: 0x%08llX\n",
           (unsigned long long)hdr.entry_point,
           (unsigned long long)hdr.load_address);
    printf(".text  offset=0x%llX  size=%llu\n",
           (unsigned long long)hdr.text_offset,
           (unsigned long long)hdr.text_size);

    // Read .text
    std::vector<uint8_t> text(hdr.text_size);
    f.seekg((std::streamoff)hdr.text_offset);
    f.read(reinterpret_cast<char*>(text.data()), hdr.text_size);

    printf("\n.text:\n");
    uint64_t load = hdr.load_address ? hdr.load_address : DEFAULT_LOAD_ADDR;
    for (size_t i = 0; i + 3 < text.size(); i += 4) {
        uint32_t raw = text[i] | ((uint32_t)text[i+1]<<8)
                     | ((uint32_t)text[i+2]<<16) | ((uint32_t)text[i+3]<<24);
        uint64_t addr = load + i;
        std::string dis = disassemble(addr, raw);
        printf("  %08llX:  %02X %02X %02X %02X   %s",
               (unsigned long long)addr,
               text[i], text[i+1], text[i+2], text[i+3],
               dis.c_str());
        // Mark entry point
        if (addr == hdr.entry_point) printf("  <-- ENTRY");
        printf("\n");
    }

    // Optionally dump .data
    if (show_data && hdr.data_size > 0) {
        std::vector<uint8_t> data(hdr.data_size);
        f.seekg((std::streamoff)hdr.data_offset);
        f.read(reinterpret_cast<char*>(data.data()), hdr.data_size);

        uint64_t data_va = load + hdr.text_size;
        printf("\n.data (VA=0x%llX, %llu bytes):\n",
               (unsigned long long)data_va,
               (unsigned long long)hdr.data_size);

        for (size_t i = 0; i < data.size(); i += 16) {
            printf("  %08llX: ", (unsigned long long)(data_va + i));
            for (size_t j = 0; j < 16 && i+j < data.size(); ++j)
                printf("%02X ", data[i+j]);
            printf(" |");
            for (size_t j = 0; j < 16 && i+j < data.size(); ++j) {
                char c = (char)data[i+j];
                printf("%c", (c>=0x20 && c<0x7F) ? c : '.');
            }
            printf("|\n");
        }
    }

    return 0;
}
