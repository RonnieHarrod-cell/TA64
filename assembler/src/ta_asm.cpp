// ============================================================
//  ta-asm — TA64 Assembler
//  Two-pass assembler producing TAEXE binaries.
//
//  Syntax overview:
//    ; comment
//    .section .text
//    .section .data
//    .equ NAME, value
//    .byte  val [, val ...]
//    .word  val [, val ...]
//    .qword val [, val ...]
//    .string "hello\n"
//    .align N
//    LABEL:
//    MNEMONIC [ARG1 [, ARG2 [, ARG3]]]
// ============================================================
#include "ta64_isa.hpp"
#include "loader.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace TA64;

// ─── Utility ────────────────────────────────────────────────
static std::string trim(std::string s) {
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}
static std::string to_upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(c));
    return s;
}
static std::string strip_comment(const std::string& s) {
    auto p = s.find(';');
    return (p == std::string::npos) ? s : s.substr(0, p);
}

// ─── Register name → index ──────────────────────────────────
static bool parse_reg(const std::string& tok, uint8_t& out) {
    std::string up = to_upper(tok);
    if (up == "SP") { out = SP_IDX; return true; }
    if (up == "PC") { out = PC_IDX; return true; }
    if (up == "FL") { out = FL_IDX; return true; }
    if (up.size() >= 2 && up[0] == 'R') {
        int n = std::stoi(up.substr(1));
        if (n >= 0 && n <= 15) { out = static_cast<uint8_t>(n); return true; }
    }
    return false;
}

// ─── Integer literal parser (dec / hex / char) ──────────────
static bool parse_int(const std::string& s, int64_t& out) {
    std::string t = trim(s);
    if (t.empty()) return false;
    if (t.size() >= 3 && t[0] == '\'' && t.back() == '\'') {
        // char literal 'A'
        out = t[1];
        return true;
    }
    try {
        size_t pos;
        if (t.size() > 2 && t[0] == '0' && (t[1]=='x' || t[1]=='X'))
            out = static_cast<int64_t>(std::stoull(t, &pos, 16));
        else
            out = std::stoll(t, &pos, 10);
        return pos == t.size();
    } catch (...) { return false; }
}

// ─── Token splitter ─────────────────────────────────────────
static std::vector<std::string> split_args(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool instr = false;
    for (char c : s) {
        if (c == '"')      { instr = !instr; cur += c; }
        else if (!instr && c == ',') { out.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

// ─── Section type ───────────────────────────────────────────
enum class Section { TEXT, DATA, BSS, NONE };

// ─── Assembled line ─────────────────────────────────────────
struct Line {
    int      src_line;
    Section  section;
    uint64_t offset;   // within section
    std::vector<uint8_t> bytes;
    // For instructions whose label references are patched in pass 2:
    bool     needs_patch   = false;
    std::string patch_label;
    uint64_t patch_off     = 0; // byte offset within `bytes` to patch
    bool     patch_is_call = false; // CALL vs jump patching
};

// ─── Symbol table entry ─────────────────────────────────────
struct Symbol {
    Section  section;
    uint64_t offset;
};

// ─── Assembler ──────────────────────────────────────────────
class Assembler {
public:
    bool assemble(const std::string& src_path, const std::string& out_path) {
        // Read source
        std::ifstream f(src_path);
        if (!f) { error(0, "Cannot open: " + src_path); return false; }
        std::vector<std::string> lines;
        std::string ln;
        while (std::getline(f, ln)) lines.push_back(ln);

        pass1(lines);
        if (had_error_) return false;
        pass2();
        if (had_error_) return false;
        emit(out_path);
        return !had_error_;
    }

private:
    // ── State ────────────────────────────────────────────────
    std::vector<Line>           text_lines_, data_lines_;
    std::map<std::string,Symbol> symbols_;
    std::map<std::string,int64_t> equates_;
    Section  cur_section_ = Section::TEXT;
    uint64_t text_off_    = 0, data_off_ = 0, bss_off_ = 0;
    bool     had_error_   = false;
    uint64_t entry_       = DEFAULT_LOAD_ADDR;
    bool     entry_set_   = false;

    void error(int ln, const std::string& msg) {
        std::cerr << "Error (line " << ln << "): " << msg << "\n";
        had_error_ = true;
    }

    // ── Resolve a token: register, immediate, or label ──────
    bool resolve_sym(const std::string& tok, int64_t& val, int src_ln) {
        // Check equate
        auto it = equates_.find(to_upper(tok));
        if (it != equates_.end()) { val = it->second; return true; }
        // Check label
        auto it2 = symbols_.find(tok);
        if (it2 != symbols_.end()) {
            uint64_t base = (it2->second.section == Section::TEXT)
                ? DEFAULT_LOAD_ADDR : DEFAULT_LOAD_ADDR + text_size();
            val = static_cast<int64_t>(base + it2->second.offset);
            return true;
        }
        // Try immediate
        return parse_int(tok, val);
    }

    uint64_t text_size() const {
        uint64_t sz = 0;
        for (auto& l : text_lines_) sz += l.bytes.size();
        return sz;
    }
    uint64_t data_size() const {
        uint64_t sz = 0;
        for (auto& l : data_lines_) sz += l.bytes.size();
        return sz;
    }

    // ── PASS 1: label collection + pseudo-directives ─────────
    void pass1(const std::vector<std::string>& raw) {
        int src_ln = 0;
        for (auto& raw_ln : raw) {
            ++src_ln;
            std::string ln = trim(strip_comment(raw_ln));
            if (ln.empty()) continue;

            // Label?
            if (auto colon = ln.find(':'); colon != std::string::npos) {
                std::string label = trim(ln.substr(0, colon));
                symbols_[label] = { cur_section_,
                    cur_section_ == Section::TEXT ? text_off_ :
                    cur_section_ == Section::DATA ? data_off_ : bss_off_ };
                if (!entry_set_) { entry_ = DEFAULT_LOAD_ADDR + text_off_; entry_set_ = true; }
                ln = trim(ln.substr(colon + 1));
                if (ln.empty()) continue;
            }

            // Directive?
            if (ln[0] == '.') {
                handle_directive(ln, src_ln);
                continue;
            }

            // Instruction placeholder (4 bytes in .text, variable in .data)
            if (cur_section_ == Section::TEXT) {
                text_off_ += 4;
                // We'll assemble in pass2
            }
            // data instructions are handled by directives
        }
    }

    void handle_directive(const std::string& ln, int src_ln) {
        std::istringstream ss(ln);
        std::string dir; ss >> dir;
        dir = to_upper(dir);
        std::string rest; std::getline(ss, rest); rest = trim(rest);

        if (dir == ".SECTION") {
            std::string sec = to_upper(trim(rest));
            if (sec == ".TEXT" || sec == "TEXT") cur_section_ = Section::TEXT;
            else if (sec == ".DATA" || sec == "DATA") cur_section_ = Section::DATA;
            else if (sec == ".BSS"  || sec == "BSS")  cur_section_ = Section::BSS;
            else error(src_ln, "Unknown section: " + rest);
        } else if (dir == ".EQU" || dir == ".DEFINE") {
            auto comma = rest.find(',');
            if (comma == std::string::npos) { error(src_ln, ".equ needs ,"); return; }
            std::string name = to_upper(trim(rest.substr(0, comma)));
            int64_t val;
            if (!parse_int(trim(rest.substr(comma+1)), val))
                { error(src_ln, ".equ: bad value"); return; }
            equates_[name] = val;
        } else if (dir == ".ENTRY") {
            int64_t v; if (parse_int(rest, v)) { entry_ = v; entry_set_ = true; }
        } else if (dir == ".BYTE") {
            for (auto& t : split_args(rest)) {
                int64_t v = 0; resolve_sym(t, v, src_ln);
                if (cur_section_ == Section::DATA) data_off_++;
            }
        } else if (dir == ".WORD") {
            for (auto& t : split_args(rest)) {
                (void)t;
                if (cur_section_ == Section::DATA) data_off_ += 2;
            }
        } else if (dir == ".QWORD" || dir == ".DWORD") {
            for (auto& t : split_args(rest)) {
                (void)t;
                if (cur_section_ == Section::DATA) data_off_ += 8;
            }
        } else if (dir == ".STRING" || dir == ".ASCIZ") {
            // Strip quotes
            if (rest.size() >= 2 && rest.front()=='"') {
                std::string s = rest.substr(1, rest.size()-2);
                data_off_ += s.size() + (dir == ".ASCIZ" ? 1 : 0);
            }
        } else if (dir == ".ALIGN") {
            int64_t n = 4;
            parse_int(rest, n);
            if (cur_section_ == Section::TEXT) {
                while (text_off_ % n) text_off_++;
            } else {
                while (data_off_ % n) data_off_++;
            }
        } else if (dir == ".RESB") {
            int64_t n=0; parse_int(rest,n);
            bss_off_ += n;
        }
        // Unknown directives silently ignored
    }

    // ── PASS 2: full instruction encoding ───────────────────
    void pass2() {
        // Re-scan source to produce actual bytes
        // We keep a mini re-parse because pass1 collected symbols.
        // This is a simplified re-scan approach:
        // Store raw lines with their section during pass1.
        // For a clean implementation, we re-do the scan.
        // (Production assemblers store an IR; here we re-read.)
    }

    // ── Instruction encoder ──────────────────────────────────
    // Called during the actual assembly step.
public:
    // Full assemble-from-string for convenience (used by compiler backend)
    std::vector<uint8_t> assemble_text(const std::vector<std::string>& src_lines,
                                       std::string& err_out,
                                       uint64_t& entry_out,
                                       std::vector<uint8_t>& data_out);
private:

    bool emit(const std::string& path) {
        // Called by assemble(); the full pipeline is in assemble_text.
        // This version is a stub; assemble() uses assemble_text() directly.
        return !had_error_;
    }
};

// ══════════════════════════════════════════════════════════════
//  FULL TWO-PASS ASSEMBLER (self-contained, no class state)
// ══════════════════════════════════════════════════════════════

struct AsmResult {
    std::vector<uint8_t> text, data;
    uint64_t bss_size = 0;
    uint64_t entry    = DEFAULT_LOAD_ADDR;
};

class TwoPassAssembler {
public:
    bool run(const std::string& src_path, const std::string& out_path);
    std::string last_error;

private:
    // ── Symbol / equate tables ───────────────────────────────
    struct LabelEntry {
        Section  sec;
        uint64_t offset; // section-relative
    };
    std::map<std::string, LabelEntry> label_map_;  // section-relative
    std::map<std::string, int64_t>   equates_;    // .equ constants

    // Resolve a label to absolute VA (called after both sections assembled)
    uint64_t label_va(const LabelEntry& e) const {
        if (e.sec == Section::TEXT) return DEFAULT_LOAD_ADDR + e.offset;
        if (e.sec == Section::DATA) return DEFAULT_LOAD_ADDR + text_.size() + e.offset;
        return DEFAULT_LOAD_ADDR + text_.size() + data_.size() + e.offset;
    }

    // labels_ maps name → absolute VA (computed lazily after both sections done)
    // We'll populate this map after assembly is complete.
    std::map<std::string, uint64_t> labels_;

    // ── Sections ─────────────────────────────────────────────
    std::vector<uint8_t> text_, data_;
    uint64_t             bss_sz_  = 0;
    Section              sec_     = Section::TEXT;
    uint64_t             text_pc_ = DEFAULT_LOAD_ADDR;
    uint64_t             data_pc_ = 0; // relative offset within data
    uint64_t             entry_   = DEFAULT_LOAD_ADDR;
    bool                 entry_set_ = false;

    // ── Patch requests (for forward-referenced labels) ───────
    struct Patch {
        uint64_t  ins_va;   // VA of instruction in .text
        uint32_t  ins_idx;  // byte index within text_
        std::string label;
        enum class Kind { JUMP_SIMM16, CALL_SIMM16, ABS16 } kind;
    };
    std::vector<Patch> patches_;

    bool error(int ln, const std::string& m) {
        last_error = "Error line " + std::to_string(ln) + ": " + m;
        std::cerr << last_error << "\n";
        return false;
    }

    // ── Helpers ──────────────────────────────────────────────
    static std::string ucase(std::string s) {
        for (auto& c : s) c = (char)toupper(c);
        return s;
    }

    uint8_t reg_of(const std::string& t, int ln, bool& ok) {
        uint8_t r;
        std::string u = ucase(trim(t));
        // Strip leading # for immediate indicator
        if (!u.empty() && u[0]=='#') u = u.substr(1);
        if (parse_reg(u, r)) { ok = true; return r; }
        ok = false; error(ln, "Unknown register: " + t); return 0;
    }

    int64_t imm_of(const std::string& t, int ln, bool& ok) {
        std::string u = ucase(trim(t));
        if (!u.empty() && u[0]=='#') u = u.substr(1);
        // Equate?
        auto it = equates_.find(u);
        if (it != equates_.end()) { ok = true; return it->second; }
        // Resolved label (from labels_ map, populated after full assembly)?
        auto it2 = labels_.find(trim(t));
        if (it2 != labels_.end()) { ok = true; return (int64_t)it2->second; }
        it2 = labels_.find(u);
        if (it2 != labels_.end()) { ok = true; return (int64_t)it2->second; }
        // Section-relative label (during assembly, before finalise)
        auto it3 = label_map_.find(trim(t));
        if (it3 != label_map_.end()) { ok = true; return (int64_t)label_va(it3->second); }
        it3 = label_map_.find(u);
        if (it3 != label_map_.end()) { ok = true; return (int64_t)label_va(it3->second); }
        int64_t v;
        if (parse_int(u, v)) { ok = true; return v; }
        ok = false; return 0;
    }

    void push32(uint32_t v) {
        text_.push_back( v        & 0xFF);
        text_.push_back((v >>  8) & 0xFF);
        text_.push_back((v >> 16) & 0xFF);
        text_.push_back((v >> 24) & 0xFF);
        text_pc_ += 4;
    }

    void push_data8 (uint8_t  v) { data_.push_back(v); data_pc_++; }
    void push_data16(uint16_t v) {
        data_.push_back(v & 0xFF);
        data_.push_back((v >> 8) & 0xFF);
        data_pc_ += 2;
    }
    void push_data64(uint64_t v) {
        for (int i=0;i<8;i++) { data_.push_back(v & 0xFF); v >>= 8; }
        data_pc_ += 8;
    }

    // ── Process one source line ──────────────────────────────
    bool process_line(const std::string& raw, int ln);

    // ── Apply forward-reference patches ─────────────────────
    bool apply_patches(int pass);
};

bool TwoPassAssembler::process_line(const std::string& raw, int ln) {
    std::string line = trim(strip_comment(raw));
    if (line.empty()) return true;

    // ── Label extraction ─────────────────────────────────────
    if (auto colon = line.find(':'); colon != std::string::npos) {
        std::string lbl = trim(line.substr(0, colon));
        // Store section-relative offset; final VA resolved after assembly
        if (sec_ == Section::TEXT)
            label_map_[lbl] = { Section::TEXT, (uint64_t)text_.size() };
        else if (sec_ == Section::DATA)
            label_map_[lbl] = { Section::DATA, data_pc_ };
        else
            label_map_[lbl] = { Section::BSS, bss_sz_ };

        if (!entry_set_) {
            entry_ = DEFAULT_LOAD_ADDR + (uint64_t)text_.size();
            entry_set_ = true;
        }
        line = trim(line.substr(colon + 1));
        if (line.empty()) return true;
    }

    // ── Directives ───────────────────────────────────────────
    if (line[0] == '.') {
        std::istringstream ss(line);
        std::string dir; ss >> dir; dir = ucase(dir);
        std::string rest; std::getline(ss, rest); rest = trim(rest);

        if (dir == ".SECTION") {
            std::string s = ucase(trim(rest));
            if (s == ".TEXT" || s == "TEXT")   sec_ = Section::TEXT;
            else if (s == ".DATA" || s == "DATA") sec_ = Section::DATA;
            else if (s == ".BSS"  || s == "BSS")  sec_ = Section::BSS;
        } else if (dir == ".EQU" || dir == ".DEFINE") {
            auto c = rest.find(',');
            if (c == std::string::npos) return error(ln, ".equ missing comma");
            std::string name = ucase(trim(rest.substr(0,c)));
            int64_t v; bool ok;
            v = imm_of(trim(rest.substr(c+1)), ln, ok);
            if (!ok) return error(ln, ".equ: bad value");
            equates_[name] = v;
        } else if (dir == ".ENTRY") {
            bool ok; int64_t v = imm_of(rest, ln, ok);
            if (ok) { entry_ = (uint64_t)v; entry_set_ = true; }
        } else if (dir == ".BYTE") {
            for (auto& t : split_args(rest)) {
                bool ok; int64_t v = imm_of(t, ln, ok);
                if (!ok) return error(ln, ".byte bad value: " + t);
                push_data8((uint8_t)v);
            }
        } else if (dir == ".WORD") {
            for (auto& t : split_args(rest)) {
                bool ok; int64_t v = imm_of(t, ln, ok);
                if (!ok) return error(ln, ".word bad value");
                push_data16((uint16_t)v);
            }
        } else if (dir == ".QWORD" || dir == ".DWORD") {
            for (auto& t : split_args(rest)) {
                bool ok; int64_t v = imm_of(t, ln, ok);
                if (!ok) return error(ln, ".qword bad value");
                push_data64((uint64_t)v);
            }
        } else if (dir == ".STRING") {
            // Unescape
            if (rest.size()>=2 && rest.front()=='"') {
                std::string s = rest.substr(1, rest.size()-2);
                std::string esc;
                for (size_t i=0;i<s.size();i++) {
                    if (s[i]=='\\' && i+1<s.size()) {
                        switch(s[++i]) {
                        case 'n': esc+='\n'; break;
                        case 't': esc+='\t'; break;
                        case '0': esc+='\0'; break;
                        default: esc+=s[i]; break;
                        }
                    } else esc += s[i];
                }
                for (char c : esc) push_data8((uint8_t)c);
            }
        } else if (dir == ".ASCIZ") {
            if (rest.size()>=2 && rest.front()=='"') {
                std::string s = rest.substr(1, rest.size()-2);
                for (char c : s) push_data8((uint8_t)c);
                push_data8(0);
            }
        } else if (dir == ".RESB") {
            bool ok; int64_t n = 0; n = imm_of(rest,ln,ok);
            bss_sz_ += (uint64_t)n;
        } else if (dir == ".ALIGN") {
            bool ok; int64_t n = 4; imm_of(rest, ln, ok);
            if (sec_ == Section::TEXT)
                while (text_.size() % n) push32(0); // NOP
            else
                while (data_.size() % n) push_data8(0);
        }
        return true;
    }

    // ── Instruction ──────────────────────────────────────────
    if (sec_ != Section::TEXT) return true; // skip instructions outside .text

    std::istringstream ss(line);
    std::string mnem; ss >> mnem; mnem = ucase(mnem);
    std::string operands; std::getline(ss, operands);
    auto args = split_args(trim(operands));

    uint64_t cur_pc = text_pc_;

    // ── NOP / HLT / RET / IRET ──────────────────────────────
    if (mnem=="NOP")  { push32(encode_instruction(Opcode::NOP, 0,0,0)); return true; }
    if (mnem=="HLT")  { push32(encode_instruction(Opcode::HLT, 0,0,0)); return true; }
    if (mnem=="RET")  { push32(encode_instruction(Opcode::RET, 0,0,0)); return true; }
    if (mnem=="IRET") { push32(encode_instruction(Opcode::IRET,0,0,0)); return true; }

    // ── INT ──────────────────────────────────────────────────
    if (mnem=="INT") {
        bool ok; int64_t v = imm_of(args[0],ln,ok);
        push32(encode_instruction(Opcode::INT,(uint8_t)v,0,0)); return true;
    }

    // ── MOV ──────────────────────────────────────────────────
    if (mnem=="MOV") {
        if (args.size() < 2) return error(ln, "MOV needs 2 args");
        bool ok; uint8_t dst = reg_of(args[0],ln,ok);
        if (!ok) return false;
        uint8_t src_r;
        if (parse_reg(ucase(trim(args[1])), src_r)) {
            push32(encode_instruction(Opcode::MOV, dst, src_r, 0));
        } else {
            // Immediate: try #imm8 via MOVI for >8-bit
            std::string a = trim(args[1]);
            if (!a.empty() && a[0]=='#') a = a.substr(1);
            int64_t v; bool ov;
            v = imm_of(a, ln, ov);
            if (!ov) {
                // Forward label — emit MOVI with patch
                uint32_t idx = (uint32_t)text_.size();
                push32(encode_instruction(Opcode::MOVI, dst, 0, 0));
                patches_.push_back({cur_pc, idx, trim(args[1]),
                    Patch::Kind::ABS16});
            } else if (v >= 0 && v <= 255) {
                push32(encode_instruction(Opcode::MOV, dst | IMM_FLAG, (uint8_t)v, 0));
            } else {
                uint8_t lo = v & 0xFF, hi = (v >> 8) & 0xFF;
                push32(encode_instruction(Opcode::MOVI, dst, lo, hi));
            }
        }
        return true;
    }

    // ── MOVI ─────────────────────────────────────────────────
    if (mnem=="MOVI") {
        if (args.size()<2) return error(ln,"MOVI needs 2 args");
        bool ok; uint8_t dst = reg_of(args[0],ln,ok); if(!ok) return false;
        bool ov; int64_t v = imm_of(args[1],ln,ov);
        if (!ov) {
            uint32_t idx = (uint32_t)text_.size();
            push32(encode_instruction(Opcode::MOVI, dst, 0, 0));
            patches_.push_back({cur_pc, idx, trim(args[1]), Patch::Kind::ABS16});
        } else {
            uint8_t lo = v & 0xFF, hi = (v>>8) & 0xFF;
            push32(encode_instruction(Opcode::MOVI, dst, lo, hi));
        }
        return true;
    }

    // ── INC / DEC / NOT / PUSH / POP ────────────────────────
    auto single_reg_op = [&](Opcode op) {
        bool ok; uint8_t r = reg_of(args[0],ln,ok);
        if (ok) push32(encode_instruction(op, r, 0, 0));
        return ok;
    };
    if (mnem=="INC")  return single_reg_op(Opcode::INC);
    if (mnem=="DEC")  return single_reg_op(Opcode::DEC);
    if (mnem=="NOT")  return single_reg_op(Opcode::NOT);
    if (mnem=="PUSH") return single_reg_op(Opcode::PUSH);
    if (mnem=="POP")  return single_reg_op(Opcode::POP);

    // ── Two-register ALU ────────────────────────────────────
    auto two_reg_op = [&](Opcode op) {
        if (args.size()<2) return error(ln, mnem + " needs 2 args");
        bool ok1, ok2;
        uint8_t r1 = reg_of(args[0],ln,ok1), r2 = reg_of(args[1],ln,ok2);
        if (ok1 && ok2) push32(encode_instruction(op,r1,r2,0));
        return ok1 && ok2;
    };
    if (mnem=="ADD") return two_reg_op(Opcode::ADD);
    if (mnem=="SUB") return two_reg_op(Opcode::SUB);
    if (mnem=="MUL") return two_reg_op(Opcode::MUL);
    if (mnem=="DIV") return two_reg_op(Opcode::DIV);
    if (mnem=="MOD") return two_reg_op(Opcode::MOD);
    if (mnem=="AND") return two_reg_op(Opcode::AND);
    if (mnem=="OR")  return two_reg_op(Opcode::OR);
    if (mnem=="XOR") return two_reg_op(Opcode::XOR);
    if (mnem=="SHL") return two_reg_op(Opcode::SHL);
    if (mnem=="SHR") return two_reg_op(Opcode::SHR);
    if (mnem=="CMP") return two_reg_op(Opcode::CMP);

    // ── LOAD / STORE ─────────────────────────────────────────
    if (mnem=="LOAD" || mnem=="STORE") {
        // LOAD  Rdst, [Rbase]  or  LOAD Rdst, [Rbase + imm8]
        // STORE Rsrc, [Rbase]  or  STORE Rsrc, [Rbase + imm8]
        if (args.size()<2) return error(ln, mnem + " needs 2 args");
        bool ok; uint8_t r1 = reg_of(args[0],ln,ok); if(!ok) return false;
        std::string mem_arg = trim(args[1]);
        // Strip brackets
        if (mem_arg.front()=='[') mem_arg = mem_arg.substr(1);
        if (mem_arg.back()==']')  mem_arg.pop_back();
        // Split on + or -
        uint8_t rbase = 0; int8_t offset = 0;
        auto plus = mem_arg.find('+');
        auto minus = mem_arg.rfind('-');
        if (plus != std::string::npos) {
            rbase = reg_of(trim(mem_arg.substr(0,plus)),ln,ok); if(!ok) return false;
            bool ov; int64_t v = imm_of(trim(mem_arg.substr(plus+1)),ln,ov);
            offset = (int8_t)(ov ? v : 0);
        } else if (minus != std::string::npos && minus > 0) {
            rbase = reg_of(trim(mem_arg.substr(0,minus)),ln,ok); if(!ok) return false;
            bool ov; int64_t v = imm_of(trim(mem_arg.substr(minus+1)),ln,ov);
            offset = (int8_t)(ov ? -v : 0);
        } else {
            rbase = reg_of(trim(mem_arg),ln,ok); if(!ok) return false;
        }
        Opcode op = (mnem=="LOAD") ? Opcode::LOAD : Opcode::STORE;
        push32(encode_instruction(op, r1, rbase, (uint8_t)offset));
        return true;
    }

    // ── LEA ──────────────────────────────────────────────────
    if (mnem=="LEA") {
        if (args.size()<2) return error(ln,"LEA needs 2 args");
        bool ok; uint8_t dst = reg_of(args[0],ln,ok); if(!ok) return false;
        uint32_t idx = (uint32_t)text_.size();
        push32(encode_instruction(Opcode::MOVI, dst, 0, 0)); // MOVI = load abs addr
        patches_.push_back({cur_pc, idx, trim(args[1]), Patch::Kind::ABS16});
        return true;
    }

    // ── Branches ────────────────────────────────────────────
    auto branch_op = [&](Opcode op) {
        if (args.empty()) return error(ln, mnem + " needs 1 arg");
        uint8_t r;
        if (parse_reg(ucase(trim(args[0])), r)) {
            push32(encode_instruction(op, r, 0, 0));
            return true;
        }
        // Label or immediate offset
        bool ok; int64_t target = 0;
        target = imm_of(args[0], ln, ok);
        uint32_t ins_idx = (uint32_t)text_.size();
        if (!ok) {
            // Forward label
            push32(encode_instruction(op, IMM_FLAG, 0, 0));
            Patch::Kind kind = (op==Opcode::CALL) ? Patch::Kind::CALL_SIMM16 : Patch::Kind::JUMP_SIMM16;
            patches_.push_back({cur_pc, ins_idx, trim(args[0]), kind});
        } else {
            int64_t delta = (target - (int64_t)(cur_pc + 4)) / 4;
            if (delta < -32768 || delta > 32767) return error(ln, "Branch out of range");
            int16_t sd = (int16_t)delta;
            uint8_t lo = sd & 0xFF, hi = (sd >> 8) & 0xFF;
            push32(encode_instruction(op, IMM_FLAG, lo, hi));
        }
        return true;
    };
    if (mnem=="JMP")  return branch_op(Opcode::JMP);
    if (mnem=="JE")   return branch_op(Opcode::JE);
    if (mnem=="JNE")  return branch_op(Opcode::JNE);
    if (mnem=="JLT")  return branch_op(Opcode::JLT);
    if (mnem=="JGT")  return branch_op(Opcode::JGT);
    if (mnem=="JLE")  return branch_op(Opcode::JLE);
    if (mnem=="JGE")  return branch_op(Opcode::JGE);
    if (mnem=="CALL") return branch_op(Opcode::CALL);

    return error(ln, "Unknown mnemonic: " + mnem);
}

bool TwoPassAssembler::apply_patches(int /*pass*/) {
    // First finalise all labels_ from label_map_ now that text_.size() is known
    for (auto& [name, entry] : label_map_)
        labels_[name] = label_va(entry);

    for (auto& p : patches_) {
        auto it = labels_.find(p.label);
        if (it == labels_.end()) it = labels_.find(ucase(p.label));
        if (it == labels_.end()) {
            last_error = "Undefined label: " + p.label;
            std::cerr << last_error << "\n";
            return false;
        }
        uint64_t target = it->second;
        uint32_t idx    = p.ins_idx;

        if (p.kind == Patch::Kind::ABS16) {
            // Patch MOVI low/hi bytes (arg2=lo, arg3=hi)
            uint8_t lo = target & 0xFF, hi = (target >> 8) & 0xFF;
            text_[idx + 2] = lo;
            text_[idx + 3] = hi;
        } else {
            // Branch: signed 16-bit instruction-count delta
            int64_t delta = ((int64_t)target - (int64_t)(p.ins_va + 4)) / 4;
            if (delta < -32768 || delta > 32767) {
                last_error = "Branch to " + p.label + " out of range";
                std::cerr << last_error << "\n";
                return false;
            }
            int16_t sd = (int16_t)delta;
            text_[idx + 2] = (uint8_t)(sd & 0xFF);
            text_[idx + 3] = (uint8_t)((sd >> 8) & 0xFF);
        }
    }
    return true;
}

bool TwoPassAssembler::run(const std::string& src_path, const std::string& out_path) {
    std::ifstream f(src_path);
    if (!f) {
        last_error = "Cannot open: " + src_path;
        std::cerr << last_error << "\n";
        return false;
    }
    std::vector<std::string> lines;
    std::string ln;
    while (std::getline(f, ln)) lines.push_back(ln);

    // ── PASS 1: collect labels from .section + label defs ───
    // We do a single-pass with forward-ref patching.
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (!process_line(lines[i], i+1)) return false;
    }
    // apply_patches finalises labels_ from label_map_ now that text_.size() is known
    if (!apply_patches(2)) return false;

    // Fix entry_ if it was set relative to a TEXT label recorded during DATA processing
    if (entry_set_) {
        // entry_ was set as: DEFAULT_LOAD_ADDR + text_.size() at time of first label in TEXT
        // But if text_ was empty then (because .data came first), it's wrong.
        // Re-check: find "_start" or first TEXT label
        for (auto& [name, e] : label_map_) {
            if (e.sec == Section::TEXT && e.offset == 0) {
                entry_ = DEFAULT_LOAD_ADDR + e.offset;
                break;
            }
        }
        // Or re-use whatever _start resolves to
        if (auto it = labels_.find("_start"); it != labels_.end())
            entry_ = it->second;
    }

    // ── Emit TAEXE ───────────────────────────────────────────
    try {
        BinaryLoader::write_taexe(out_path, text_, data_, entry_, DEFAULT_LOAD_ADDR);
    } catch (const std::exception& e) {
        last_error = e.what();
        std::cerr << last_error << "\n";
        return false;
    }

    printf("[ta-asm] Assembled %s → %s\n", src_path.c_str(), out_path.c_str());
    printf("  .text  %zu bytes   entry=0x%08llX\n",
           text_.size(), (unsigned long long)entry_);
    if (!data_.empty())
        printf("  .data  %zu bytes\n", data_.size());
    if (bss_sz_)
        printf("  .bss   %llu bytes\n", (unsigned long long)bss_sz_);
    return true;
}

// ─── Main ───────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: ta-asm <source.asm> <output.taexe>\n";
        return 1;
    }
    TwoPassAssembler asm_;
    bool ok = asm_.run(argv[1], argv[2]);
    return ok ? 0 : 1;
}
