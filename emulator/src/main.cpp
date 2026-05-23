// ============================================================
//  TA64 Emulator — main entry point
//  Loads a TAEXE binary and runs it.
//  With --display flag: opens an SDL2 terminal window.
//  Without SDL2: pure-console mode (still fully functional).
// ============================================================
#include "ta64_isa.hpp"
#include "memory.hpp"
#include "cpu.hpp"
#include "loader.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#endif

using namespace TA64;

// ─── CLI help ───────────────────────────────────────────────
static void usage(const char* argv0) {
    std::cerr <<
        "Usage: " << argv0 << " [options] <binary.taexe>\n"
        "Options:\n"
        "  -t, --trace          Enable instruction trace\n"
        "  -s, --step           Single-step mode\n"
        "  -b, --break <addr>   Set breakpoint at hex address\n"
        "  -d, --dump <n>       Dump first N bytes of memory after run\n"
        "  -r, --regs           Print register dump after run\n"
        "  --display            Open SDL2 terminal window\n"
        "  -h, --help           This help\n";
}

// ─── Console-mode output buffer (for SDL terminal) ──────────
static std::string console_buf;

#ifdef HAVE_SDL2
// ────────────────────────────────────────────────────────────
//  Minimal SDL2 terminal: renders text buffer in a window.
//  The TA64 program writes to console_buf via SYS_WRITE.
// ────────────────────────────────────────────────────────────
struct SDLTerminal {
    SDL_Window*   win  = nullptr;
    SDL_Renderer* rend = nullptr;
    TTF_Font*     font = nullptr;
    bool          ok   = false;

    static constexpr int W  = 800, H  = 600;
    static constexpr int FW = 10,  FH = 18;

    bool init() {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
        if (TTF_Init() < 0)               return false;
        win = SDL_CreateWindow("TA64 Emulator",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            W, H, SDL_WINDOW_SHOWN);
        if (!win) return false;
        rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
        if (!rend) return false;
        // Try to load a monospace font from common paths
        const char* fonts[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
            "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
            nullptr
        };
        for (int i = 0; fonts[i]; ++i) {
            font = TTF_OpenFont(fonts[i], 16);
            if (font) break;
        }
        if (!font) {
            std::cerr << "[SDL] No monospace font found – text output disabled\n";
        }
        ok = true;
        return true;
    }

    void render(const std::string& text) {
        SDL_SetRenderDrawColor(rend, 20, 20, 20, 255);
        SDL_RenderClear(rend);

        if (font) {
            SDL_Color fg = {0, 255, 80, 255};
            int y = 4;
            std::istringstream ss(text);
            std::string line;
            while (std::getline(ss, line) && y < H - FH) {
                if (line.empty()) { y += FH; continue; }
                SDL_Surface* surf = TTF_RenderText_Blended(font, line.c_str(), fg);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(rend, surf);
                    SDL_Rect dst = {4, y, surf->w, surf->h};
                    SDL_RenderCopy(rend, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                    SDL_FreeSurface(surf);
                }
                y += FH;
            }
        }
        SDL_RenderPresent(rend);
    }

    void destroy() {
        if (font) TTF_CloseFont(font);
        if (rend) SDL_DestroyRenderer(rend);
        if (win)  SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
    }
};
#include <sstream>
#endif

// ─── Main ───────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    bool        trace        = false;
    bool        single_step  = false;
    bool        dump_regs    = false;
    [[maybe_unused]] bool use_sdl = false;
    uint64_t    dump_bytes   = 0;
    std::string binary;
    std::vector<uint64_t> breakpoints;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-t" || a == "--trace")    trace       = true;
        else if (a == "-s" || a == "--step") single_step = true;
        else if (a == "-r" || a == "--regs") dump_regs   = true;
        else if (a == "--display")           use_sdl     = true;
        else if ((a == "-b" || a == "--break") && i+1 < argc)
            breakpoints.push_back(std::stoull(argv[++i], nullptr, 16));
        else if ((a == "-d" || a == "--dump") && i+1 < argc)
            dump_bytes = std::stoull(argv[++i]);
        else if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else binary = a;
    }

    if (binary.empty()) { usage(argv[0]); return 1; }

    try {
        Memory mem;
        CPU    cpu(mem);

        // Install syscall handler that also feeds console_buf
        cpu.set_syscall_handler([&](CPU& c) {
            uint64_t num = c.reg(0);
            switch (num) {
            case Syscall::SYS_EXIT:
                c.fl() |= Flags::HALT;
                break;
            case Syscall::SYS_WRITE: {
                uint64_t addr = c.reg(2);
                uint64_t len  = c.reg(3);
                for (uint64_t i = 0; i < len; ++i) {
                    char ch = (char)mem.read8(addr + i);
                    std::cout << ch;
                    console_buf += ch;
                }
                std::cout.flush();
                c.reg(0) = len;
                break;
            }
            case Syscall::SYS_GETPID:
                c.reg(0) = 1;
                break;
            default:
                std::cerr << "[SYSCALL] Unknown: " << num << "\n";
                break;
            }
        });

        BinaryLoader::load(binary, mem, cpu);

        cpu.set_trace(trace);
        cpu.set_single_step(single_step);
        for (auto bp : breakpoints)
            cpu.add_breakpoint(bp);

#ifdef HAVE_SDL2
        SDLTerminal terminal;
        if (use_sdl) {
            if (!terminal.init()) {
                std::cerr << "[SDL] Init failed: " << SDL_GetError() << "\n";
                use_sdl = false;
            }
        }
        if (use_sdl) {
            // Run in background; pump SDL events each quantum
            constexpr uint64_t QUANTUM = 10000;
            bool quit = false;
            SDL_Event evt;
            while (!(cpu.fl() & Flags::HALT) && !quit) {
                cpu.run(QUANTUM);
                while (SDL_PollEvent(&evt))
                    if (evt.type == SDL_QUIT) quit = true;
                terminal.render(console_buf);
            }
            // Hold window open until closed
            while (!quit) {
                while (SDL_PollEvent(&evt))
                    if (evt.type == SDL_QUIT) quit = true;
                terminal.render(console_buf);
                SDL_Delay(16);
            }
            terminal.destroy();
        } else {
#else
        {
#endif
            cpu.run();
        }

        if (dump_regs)  cpu.dump_registers();
        if (dump_bytes) cpu.dump_memory(DEFAULT_LOAD_ADDR, dump_bytes);

        std::cout << "\n[TA64] Halted after " << cpu.cycles() << " cycles.\n";
        return static_cast<int>(cpu.reg(0)); // R0 = exit code

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
