#pragma once
// ============================================================
//  TA64 Memory Manager
//  Flat 1 MB address space with byte-accurate access.
//  Provides read8/write8 through read64/write64.
// ============================================================
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>

namespace TA64 {

class Memory {
public:
    static constexpr uint64_t SIZE = 0x100000; // 1 MB

    Memory() { ram_.fill(0); }

    // ── Byte access ─────────────────────────────────────────
    uint8_t read8(uint64_t addr) const {
        check(addr, 1);
        return ram_[addr];
    }
    void write8(uint64_t addr, uint8_t val) {
        check(addr, 1);
        ram_[addr] = val;
    }

    // ── 16-bit little-endian ────────────────────────────────
    uint16_t read16(uint64_t addr) const {
        check(addr, 2);
        uint16_t v;
        std::memcpy(&v, &ram_[addr], 2);
        return v;
    }
    void write16(uint64_t addr, uint16_t val) {
        check(addr, 2);
        std::memcpy(&ram_[addr], &val, 2);
    }

    // ── 32-bit little-endian ────────────────────────────────
    uint32_t read32(uint64_t addr) const {
        check(addr, 4);
        uint32_t v;
        std::memcpy(&v, &ram_[addr], 4);
        return v;
    }
    void write32(uint64_t addr, uint32_t val) {
        check(addr, 4);
        std::memcpy(&ram_[addr], &val, 4);
    }

    // ── 64-bit little-endian (primary for TA64 registers) ───
    uint64_t read64(uint64_t addr) const {
        check(addr, 8);
        uint64_t v;
        std::memcpy(&v, &ram_[addr], 8);
        return v;
    }
    void write64(uint64_t addr, uint64_t val) {
        check(addr, 8);
        std::memcpy(&ram_[addr], &val, 8);
    }

    // ── Bulk load (for binary loading) ──────────────────────
    void load(uint64_t base, const std::vector<uint8_t>& data) {
        if (base + data.size() > SIZE)
            throw std::runtime_error("load: out of bounds at 0x"
                + to_hex(base) + " size=" + std::to_string(data.size()));
        std::memcpy(&ram_[base], data.data(), data.size());
    }

    // ── Pointer to raw memory (use carefully) ───────────────
    uint8_t* raw(uint64_t addr = 0) noexcept { return &ram_[addr]; }
    const uint8_t* raw(uint64_t addr = 0) const noexcept { return &ram_[addr]; }

    // ── Hex dump utility ────────────────────────────────────
    std::string hex_dump(uint64_t start, uint64_t length, uint64_t cols = 16) const;

private:
    std::array<uint8_t, SIZE> ram_{};

    void check(uint64_t addr, uint64_t width) const {
        if (addr + width > SIZE)
            throw std::runtime_error("Memory access out of bounds: 0x" + to_hex(addr));
    }

    static std::string to_hex(uint64_t v) {
        char buf[17];
        snprintf(buf, sizeof(buf), "%016llX", (unsigned long long)v);
        return buf;
    }
};

inline std::string Memory::hex_dump(uint64_t start, uint64_t length, uint64_t cols) const {
    std::string out;
    char buf[64];
    for (uint64_t i = 0; i < length; i += cols) {
        snprintf(buf, sizeof(buf), "%08llX  ", (unsigned long long)(start + i));
        out += buf;
        for (uint64_t j = 0; j < cols && i + j < length; ++j) {
            snprintf(buf, sizeof(buf), "%02X ", ram_[start + i + j]);
            out += buf;
        }
        out += " |";
        for (uint64_t j = 0; j < cols && i + j < length; ++j) {
            char c = static_cast<char>(ram_[start + i + j]);
            out += (c >= 0x20 && c < 0x7F) ? c : '.';
        }
        out += "|\n";
    }
    return out;
}

} // namespace TA64
