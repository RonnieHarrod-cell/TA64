#!/usr/bin/env bash
# ============================================================
#  build.sh — TA64 one-shot build + optional run
#
#  Usage:
#    ./build.sh                  # configure + build
#    ./build.sh run hello        # build + run examples/hello.asm
#    ./build.sh runC counter     # build + compile+run examples/counter.c
#    ./build.sh test             # build + run test suite
#    ./build.sh clean            # remove build directory
# ============================================================

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[build.sh]${NC} $*"; }
warn()  { echo -e "${YELLOW}[build.sh]${NC} $*"; }

# ── Build ────────────────────────────────────────────────────
build() {
    info "Configuring TA64..."
    mkdir -p "$BUILD"
    cmake -S "$ROOT" -B "$BUILD" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        2>&1 | grep -E "SDL2|C\+\+ std|Build type|===|--$" || true

    info "Compiling with $JOBS threads..."
    cmake --build "$BUILD" --parallel "$JOBS"
    info "Build complete.  Binaries in $BUILD/"
    ls "$BUILD"/assembler/ta-asm \
       "$BUILD"/emulator/ta64-emu \
       "$BUILD"/linker/ta-ld \
       "$BUILD"/compiler/ta-gcc \
       "$BUILD"/tools/ta-dasm \
       "$BUILD"/tools/ta-monitor 2>/dev/null | sed 's|.*/||; s|^|  • |'
}

# ── Run an .asm example ──────────────────────────────────────
run_asm() {
    local name="${1:-hello}"
    local src="$ROOT/examples/${name}.asm"
    local bin="/tmp/ta64_${name}.taexe"
    [[ -f "$src" ]] || { warn "Not found: $src"; exit 1; }
    info "Assembling $src..."
    "$BUILD/assembler/ta-asm" "$src" "$bin"
    info "Running $bin..."
    "$BUILD/emulator/ta64-emu" "$bin"
}

# ── Compile + run a .c example ───────────────────────────────
run_c() {
    local name="${1:-example}"
    local src="$ROOT/examples/${name}.c"
    local asm="/tmp/ta64_${name}.asm"
    local bin="/tmp/ta64_${name}.taexe"
    [[ -f "$src" ]] || { warn "Not found: $src"; exit 1; }
    info "Compiling $src → $asm..."
    "$BUILD/compiler/ta-gcc" "$src" -o "$asm"
    info "Assembling $asm → $bin..."
    "$BUILD/assembler/ta-asm" "$asm" "$bin"
    info "Running $bin..."
    "$BUILD/emulator/ta64-emu" "$bin" -r
}

# ── Clean ────────────────────────────────────────────────────
clean() {
    info "Removing $BUILD..."
    rm -rf "$BUILD"
    info "Done."
}

# ── Dispatch ─────────────────────────────────────────────────
CMD="${1:-build}"
shift || true

case "$CMD" in
    build)          build ;;
    run)            build; run_asm "${1:-hello}" ;;
    runC|runc|run-c) build; run_c "${1:-example}" ;;
    test)           build; bash "$ROOT/tests/run_tests.sh" ;;
    clean)          clean ;;
    *)
        echo "Usage: $0 [build|run <name>|runC <name>|test|clean]"
        exit 1
        ;;
esac
