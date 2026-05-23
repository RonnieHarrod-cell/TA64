// ============================================================
//  ta-ld — TA64 Linker
//  Links multiple TAEXE object files into one final executable.
//  Sections: .text, .data, .bss
//  Symbols are exported/imported via a simple symbol table.
// ============================================================
#include "ta64_isa.hpp"
#include "loader.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>

using namespace TA64;

// ─── Object file representation ─────────────────────────────
struct ObjectFile {
    std::string              path;
    std::vector<uint8_t>     text, data;
    uint64_t                 bss_size   = 0;
    uint64_t                 entry      = 0;
    uint64_t                 load_addr  = DEFAULT_LOAD_ADDR;

    // ── Symbols exported by this object ─────────────────────
    struct SymRef {
        std::string name;
        uint64_t    text_offset; // offset within this obj's .text
        bool        is_global;
    };

    // ── Relocations needed ───────────────────────────────────
    struct Reloc {
        uint64_t    ins_byte_offset;  // within this obj's text
        std::string target_symbol;
        enum class Type { ABS16, SIMM16_JUMP } type;
    };
    std::vector<Reloc> relocs;
};

// ─── Read a TAEXE header + sections ─────────────────────────
static ObjectFile load_object(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path);

    TAEXEHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f) throw std::runtime_error("Too small: " + path);
    if (hdr.magic[0]!='T'||hdr.magic[1]!='A'||
        hdr.magic[2]!='6'||hdr.magic[3]!='4')
        throw std::runtime_error("Bad magic: " + path);

    ObjectFile obj;
    obj.path       = path;
    obj.entry      = hdr.entry_point;
    obj.load_addr  = hdr.load_address;
    obj.bss_size   = hdr.bss_size;

    if (hdr.text_size > 0) {
        f.seekg((std::streamoff)hdr.text_offset);
        obj.text.resize(hdr.text_size);
        f.read(reinterpret_cast<char*>(obj.text.data()), hdr.text_size);
    }
    if (hdr.data_size > 0) {
        f.seekg((std::streamoff)hdr.data_offset);
        obj.data.resize(hdr.data_size);
        f.read(reinterpret_cast<char*>(obj.data.data()), hdr.data_size);
    }
    return obj;
}

// ─── Linker ─────────────────────────────────────────────────
class Linker {
public:
    void add_object(const std::string& path) {
        objects_.push_back(load_object(path));
    }

    void set_entry_symbol(const std::string& s) { entry_sym_ = s; }
    void set_output(const std::string& p)        { out_path_  = p; }

    bool link() {
        // ── Layout: concatenate .text, then .data, then .bss ──
        uint64_t text_base = DEFAULT_LOAD_ADDR;
        uint64_t text_cursor = 0;
        uint64_t data_cursor = 0;
        uint64_t bss_cursor  = 0;

        // Build per-object text offsets
        std::vector<uint64_t> obj_text_base(objects_.size());
        std::vector<uint64_t> obj_data_base(objects_.size());

        std::vector<uint8_t> final_text, final_data;
        uint64_t             final_bss = 0;

        for (size_t i = 0; i < objects_.size(); ++i) {
            obj_text_base[i] = text_cursor;
            obj_data_base[i] = data_cursor;
            text_cursor += objects_[i].text.size();
            data_cursor += objects_[i].data.size();
            bss_cursor  += objects_[i].bss_size;
            final_text.insert(final_text.end(),
                objects_[i].text.begin(), objects_[i].text.end());
            final_data.insert(final_data.end(),
                objects_[i].data.begin(), objects_[i].data.end());
            final_bss += objects_[i].bss_size;
        }

        // ── Entry point ──────────────────────────────────────
        uint64_t entry = text_base; // default: start of first object
        if (!objects_.empty())
            entry = text_base + obj_text_base[0] + (objects_[0].entry - objects_[0].load_addr);

        // ── Emit ─────────────────────────────────────────────
        printf("[ta-ld] Linking %zu object(s) → %s\n",
               objects_.size(), out_path_.c_str());
        printf("  .text  %zu bytes  @ 0x%08llX\n",
               final_text.size(), (unsigned long long)text_base);
        printf("  .data  %zu bytes\n", final_data.size());
        printf("  .bss   %llu bytes\n", (unsigned long long)final_bss);
        printf("  entry  0x%08llX\n",  (unsigned long long)entry);

        try {
            TAEXEHeader hdr{};
            hdr.version     = 1;
            hdr.entry_point = entry;
            hdr.load_address = text_base;
            hdr.text_offset  = sizeof(TAEXEHeader);
            hdr.text_size    = final_text.size();
            hdr.data_offset  = sizeof(TAEXEHeader) + final_text.size();
            hdr.data_size    = final_data.size();
            hdr.bss_size     = final_bss;

            std::ofstream f(out_path_, std::ios::binary);
            if (!f) throw std::runtime_error("Cannot write: " + out_path_);
            f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
            f.write(reinterpret_cast<const char*>(final_text.data()), final_text.size());
            if (!final_data.empty())
                f.write(reinterpret_cast<const char*>(final_data.data()), final_data.size());
        } catch (const std::exception& e) {
            std::cerr << "[ta-ld] " << e.what() << "\n";
            return false;
        }
        return true;
    }

private:
    std::vector<ObjectFile> objects_;
    std::string entry_sym_ = "_start";
    std::string out_path_  = "a.taexe";
};

// ─── Main ───────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: ta-ld -o <output.taexe> <obj1.taexe> [obj2.taexe ...]\n"
                     "       ta-ld --entry <symbol> -o <out> <objs...>\n";
        return 1;
    }

    Linker linker;
    std::string out;
    std::string entry_sym = "_start";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i+1 < argc)         out = argv[++i];
        else if (a == "--entry" && i+1 < argc) entry_sym = argv[++i];
        else linker.add_object(a);
    }

    if (out.empty()) { std::cerr << "[ta-ld] No output specified (-o)\n"; return 1; }
    linker.set_output(out);
    linker.set_entry_symbol(entry_sym);

    try {
        return linker.link() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "[ta-ld] Fatal: " << e.what() << "\n";
        return 1;
    }
}
