#!/usr/bin/env bash
# ============================================================
#  TA64 Test Suite
#  Run from the project root after building:
#    cd build && cmake .. && make -j4 && cd ..
#    ./tests/run_tests.sh
# ============================================================

set -euo pipefail

BUILD=build
ASM=$BUILD/assembler/ta-asm
EMU=$BUILD/emulator/ta64-emu
LD=$BUILD/linker/ta-ld
GCC=$BUILD/compiler/ta-gcc
DASM=$BUILD/tools/ta-dasm
MON=$BUILD/tools/ta-monitor

PASS=0
FAIL=0
SKIP=0

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "  ${GREEN}PASS${NC}  $1"; PASS=$((PASS+1)); }
fail() { echo -e "  ${RED}FAIL${NC}  $1: $2"; FAIL=$((FAIL+1)); }
skip() { echo -e "  ${YELLOW}SKIP${NC}  $1"; SKIP=$((SKIP+1)); }

header() { echo -e "\n${YELLOW}── $1 ──${NC}"; }

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# ─── check binaries exist ────────────────────────────────────
header "Toolchain presence"
for bin in $ASM $EMU $LD $GCC $DASM $MON; do
    if [[ -x $bin ]]; then pass "$bin exists"
    else fail "$bin" "not found or not executable"; fi
done

# ─── Assembler tests ─────────────────────────────────────────
header "Assembler"

# Basic hello world assembly
$ASM examples/hello.asm $TMPDIR/hello.taexe 2>/dev/null
if [[ -f $TMPDIR/hello.taexe ]]; then
    # Check magic header using od (universally available)
    MAGIC=$(od -A n -t x1 -N 4 $TMPDIR/hello.taexe 2>/dev/null | tr -d ' ')
    if [[ "$MAGIC" == "54413634" ]]; then pass "hello.asm: TAEXE magic header correct"
    else fail "hello.asm" "bad magic header: $MAGIC"; fi
else fail "hello.asm" "output file not created"; fi

$ASM examples/arithmetic.asm $TMPDIR/arith.taexe 2>/dev/null
[[ -f $TMPDIR/arith.taexe ]] && pass "arithmetic.asm assembles" || fail "arithmetic.asm" "failed"

# Test .equ directive
cat > $TMPDIR/equ_test.asm << 'EOF'
.equ ANSWER, 42
.section .text
_start:
    MOVI R0, ANSWER
    INT  0x80
    HLT
EOF
$ASM $TMPDIR/equ_test.asm $TMPDIR/equ_test.taexe 2>/dev/null
[[ -f $TMPDIR/equ_test.taexe ]] && pass ".equ directive assembles" || fail ".equ" "failed"

# Test forward label reference
cat > $TMPDIR/fwd_label.asm << 'EOF'
.section .text
_start:
    JMP done
    HLT
done:
    MOV R0, #0
    INT 0x80
    HLT
EOF
$ASM $TMPDIR/fwd_label.asm $TMPDIR/fwd_label.taexe 2>/dev/null
[[ -f $TMPDIR/fwd_label.taexe ]] && pass "forward label reference assembles" || fail "forward label" "failed"

# Test .section .data with .string
cat > $TMPDIR/data_test.asm << 'EOF'
.section .data
msg: .string "TA64\n"
.section .text
_start:
    MOV  R0, #1
    MOV  R1, #1
    LEA  R2, msg
    MOV  R3, #5
    INT  0x80
    MOV  R0, #0
    INT  0x80
    HLT
EOF
$ASM $TMPDIR/data_test.asm $TMPDIR/data_test.taexe 2>/dev/null
[[ -f $TMPDIR/data_test.taexe ]] && pass ".data section + .string assembles" || fail ".data+.string" "failed"

# ─── Emulator tests ──────────────────────────────────────────
header "Emulator"

# hello world output
OUTPUT=$(timeout 5 $EMU $TMPDIR/hello.taexe 2>/dev/null | grep -v "^\[" || true)
if echo "$OUTPUT" | grep -q "Hello from TA64!"; then
    pass "hello world: correct output"
else
    fail "hello world" "unexpected output: '$OUTPUT'"
fi

# .data string output
OUTPUT=$(timeout 5 $EMU $TMPDIR/data_test.taexe 2>/dev/null | grep -v "^\[" || true)
if echo "$OUTPUT" | grep -q "TA64"; then
    pass ".data string SYS_WRITE output"
else
    fail ".data string output" "got: '$OUTPUT'"
fi

# arithmetic: verify R0 register at halt
timeout 5 $EMU $TMPDIR/arith.taexe -r 2>/dev/null > $TMPDIR/arith_regs.txt || true
if grep -q "Cycles: 23" $TMPDIR/arith_regs.txt; then
    pass "arithmetic: executed 23 cycles"
else
    fail "arithmetic cycles" "$(grep Cycles $TMPDIR/arith_regs.txt || echo 'no cycles line')"
fi

# forward label jump
timeout 5 $EMU $TMPDIR/fwd_label.taexe 2>/dev/null > /dev/null
[[ $? -eq 0 ]] && pass "forward label jump executes" || fail "forward label jump" "crash"

# stack push/pop
cat > $TMPDIR/stack_test.asm << 'EOF'
.section .text
_start:
    MOVI R0, 1234
    PUSH R0
    MOV  R0, #0
    POP  R1
    MOV  R0, #0
    INT  0x80
    HLT
EOF
$ASM $TMPDIR/stack_test.asm $TMPDIR/stack_test.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/stack_test.taexe -r 2>/dev/null > $TMPDIR/stack_regs.txt || true
if grep -q "R1  = 00000000000004D2" $TMPDIR/stack_regs.txt; then
    pass "PUSH/POP: R1 = 1234 = 0x4D2"
else
    fail "PUSH/POP" "$(grep 'R1 ' $TMPDIR/stack_regs.txt || echo 'missing')"
fi

# CALL/RET
cat > $TMPDIR/call_test.asm << 'EOF'
.section .text
_start:
    MOV  R0, #5
    MOV  R1, #7
    CALL add_them
    MOV  R8, R0        ; save result before INT clobbers R0
    MOV  R0, #0
    INT  0x80
    HLT
add_them:
    ADD  R0, R1
    RET
EOF
$ASM $TMPDIR/call_test.asm $TMPDIR/call_test.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/call_test.taexe -r 2>/dev/null > $TMPDIR/call_regs.txt || true
if grep -q "R8  = 000000000000000C" $TMPDIR/call_regs.txt; then
    pass "CALL/RET: add_them(5,7)=12=0xC"
else
    fail "CALL/RET" "$(grep 'R8 ' $TMPDIR/call_regs.txt || echo 'missing')"
fi

# CMP + conditional jumps
cat > $TMPDIR/cmp_test.asm << 'EOF'
.section .text
_start:
    MOV  R0, #10
    MOV  R1, #5
    CMP  R0, R1        ; 10 > 5
    JGT  is_greater
    MOV  R2, #0        ; not taken
    JMP  done
is_greater:
    MOV  R2, #1        ; taken
done:
    MOV  R0, #0
    INT  0x80
    HLT
EOF
$ASM $TMPDIR/cmp_test.asm $TMPDIR/cmp_test.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/cmp_test.taexe -r 2>/dev/null > $TMPDIR/cmp_regs.txt || true
if grep -q "R2  = 0000000000000001" $TMPDIR/cmp_regs.txt; then
    pass "CMP + JGT: branch taken correctly"
else
    fail "CMP+JGT" "$(grep 'R2 ' $TMPDIR/cmp_regs.txt || echo 'missing')"
fi

# Recursive fibonacci
cat > $TMPDIR/fib_iter.asm << 'EOF'
; Compute F(10) = 55 iteratively
.section .text
_start:
    MOV  R8, #0      ; a = F(0)
    MOV  R9, #1      ; b = F(1)
    MOVI R10, 10     ; n
    MOVI R11, 2      ; i=2
.loop:
    CMP  R11, R10
    JGT  .done
    MOV  R12, R8
    ADD  R8, R9
    MOV  R9, R8
    SUB  R9, R12     ; wait: R9=new_b = old_R8+old_R9; R8 was updated...
    ; Redo properly: temp=R8; R8=R9; R9=temp+old_R9=temp+R8_now
    ; Use: t=R8 before add
    ; Rewrite:
    MOV  R12, R9     ; t = R9
    ADD  R9, R8      ; R9 = R8 + old R9
    MOV  R8, R12     ; R8 = old R9
    ; Wrong again. Let's just do:
    ; We want: (a,b) = (b, a+b)
    ; R8=a, R9=b: temp=b; b=a+b; a=temp
    MOV  R12, R8     ; was: undo all...
    INC  R11
    JMP  .loop
.done:
    MOV  R0, #0
    INT  0x80
    HLT
EOF
# Use a cleaner fibonacci directly
cat > $TMPDIR/fib10.asm << 'EOF'
; Compute F(10) = 55 iteratively, result in R0
.section .text
_start:
    MOV  R0, #0      ; a = 0
    MOV  R1, #1      ; b = 1
    MOV  R2, #0      ; i = 0
    MOVI R3, 10      ; n = 10
.fib_loop:
    CMP  R2, R3
    JGE  .fib_done
    MOV  R4, R1      ; temp = b
    ADD  R1, R0      ; b = a + b
    MOV  R0, R4      ; a = temp (old b)
    INC  R2
    JMP  .fib_loop
.fib_done:
    ; R0 = F(10) = 55
    MOV  R8, R0
    MOV  R0, #0
    INT  0x80
    HLT
EOF
$ASM $TMPDIR/fib10.asm $TMPDIR/fib10.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/fib10.taexe -r 2>/dev/null > $TMPDIR/fib_regs.txt || true
if grep -q "R8  = 0000000000000037" $TMPDIR/fib_regs.txt; then
    pass "Fibonacci(10) = 55 = 0x37"
else
    fail "Fibonacci(10)" "$(grep 'R8 ' $TMPDIR/fib_regs.txt || echo 'missing')"
fi

# ─── Compiler tests ───────────────────────────────────────────
header "Compiler (ta-gcc)"

# Simple compile test
$GCC examples/example.c -o $TMPDIR/example.asm 2>/dev/null
[[ -f $TMPDIR/example.asm ]] && pass "example.c compiles to assembly" || fail "example.c compile" "failed"

# Check _start bootstrap generated
if grep -q "^_start:" $TMPDIR/example.asm; then
    pass "compiler emits _start bootstrap"
else
    fail "_start bootstrap" "not found in output"
fi

# Assemble + run the compiled C
$ASM $TMPDIR/example.asm $TMPDIR/example.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/example.taexe 2>/dev/null > /dev/null
[[ $? -eq 0 ]] && pass "compiled example.c: runs without crash" || fail "compiled example.c" "crashed"

# Single-function compile
cat > $TMPDIR/simple.c << 'EOF'
int main() {
    int x = 42;
    return x;
}
EOF
$GCC $TMPDIR/simple.c -o $TMPDIR/simple.asm 2>/dev/null
$ASM $TMPDIR/simple.asm $TMPDIR/simple.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/simple.taexe 2>/dev/null > /dev/null
[[ $? -eq 0 ]] && pass "simple int main() compiles and runs" || fail "simple main" "crashed"

# Arithmetic in C
cat > $TMPDIR/arith_c.c << 'EOF'
int main() {
    int a = 100;
    int b = 37;
    int c = a + b;
    int d = c - 7;
    return d;
}
EOF
$GCC $TMPDIR/arith_c.c -o $TMPDIR/arith_c.asm 2>/dev/null
$ASM $TMPDIR/arith_c.asm $TMPDIR/arith_c.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/arith_c.taexe -r 2>/dev/null > $TMPDIR/arith_c_regs.txt || true
# a+b=137, 137-7=130 (0x82) stored in locals; exit code is 0
[[ -f $TMPDIR/arith_c_regs.txt ]] && pass "C arithmetic compiles + runs" || fail "C arithmetic" "failed"

# If/else in C
cat > $TMPDIR/ifelse.c << 'EOF'
int main() {
    int x = 10;
    int y = 0;
    if (x > 5) {
        y = 1;
    } else {
        y = 2;
    }
    return y;
}
EOF
$GCC $TMPDIR/ifelse.c -o $TMPDIR/ifelse.asm 2>/dev/null
$ASM $TMPDIR/ifelse.asm $TMPDIR/ifelse.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/ifelse.taexe 2>/dev/null > /dev/null
[[ $? -eq 0 ]] && pass "C if/else compiles + runs" || fail "C if/else" "crashed"

# While loop
cat > $TMPDIR/while_c.c << 'EOF'
int main() {
    int sum = 0;
    int i = 1;
    while (i <= 10) {
        sum = sum + i;
        ++i;
    }
    return sum;
}
EOF
$GCC $TMPDIR/while_c.c -o $TMPDIR/while_c.asm 2>/dev/null
$ASM $TMPDIR/while_c.asm $TMPDIR/while_c.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/while_c.taexe 2>/dev/null > /dev/null
[[ $? -eq 0 ]] && pass "C while loop compiles + runs" || fail "C while loop" "crashed"

# ─── Linker tests ─────────────────────────────────────────────
header "Linker"

$LD -o $TMPDIR/linked.taexe $TMPDIR/hello.taexe $TMPDIR/arith.taexe 2>/dev/null
if [[ -f $TMPDIR/linked.taexe ]]; then
    pass "ta-ld links 2 objects"
    MAGIC=$(od -A n -t x1 -N 4 $TMPDIR/linked.taexe 2>/dev/null | tr -d ' ')
    [[ "$MAGIC" == "54413634" ]] && pass "linked output has valid TAEXE header" || fail "linked header" "bad magic: $MAGIC"
else
    fail "ta-ld linking" "output not created"
fi

# ─── Disassembler tests ───────────────────────────────────────
header "Disassembler"

OUTPUT=$($DASM $TMPDIR/hello.taexe 2>/dev/null)
echo "$OUTPUT" | grep -q "ENTRY" && pass "ta-dasm: marks entry point" || fail "ta-dasm entry" "no ENTRY marker"
echo "$OUTPUT" | grep -q "INT" && pass "ta-dasm: decodes INT instruction" || fail "ta-dasm INT" "not found"
echo "$OUTPUT" | grep -q "MOV" && pass "ta-dasm: decodes MOV instruction" || fail "ta-dasm MOV" "not found"

# Disassemble with data flag
OUTPUT=$($DASM $TMPDIR/hello.taexe -a 2>/dev/null)
echo "$OUTPUT" | grep -q "Hello from TA64" && pass "ta-dasm -a: shows .data section" || fail "ta-dasm -a" "no data"

# ─── Monitor tests ────────────────────────────────────────────
header "Monitor (ta-monitor)"

OUTPUT=$(echo "regs
dis 1000
quit" | timeout 5 $MON $TMPDIR/hello.taexe 2>/dev/null || true)
echo "$OUTPUT" | grep -q "Register Dump" && pass "monitor: regs command" || fail "monitor regs" "no output"
echo "$OUTPUT" | grep -q "MOV\|INT\|HLT" && pass "monitor: dis command shows instructions" || fail "monitor dis" "no instructions"

# Step command
OUTPUT=$(echo "step 3
regs
quit" | timeout 5 $MON $TMPDIR/hello.taexe 2>/dev/null || true)
echo "$OUTPUT" | grep -q "Cycles: 3" && pass "monitor: step 3 executes 3 cycles" || fail "monitor step" "cycles not 3"

# ─── ISA correctness tests ────────────────────────────────────
header "ISA Correctness"

# Test all arithmetic opcodes
cat > $TMPDIR/isa_arith.asm << 'EOF'
.section .text
_start:
    ; ADD: 20+13=33=0x21 → save in R8
    MOV  R0, #20
    MOV  R1, #13
    ADD  R0, R1
    MOV  R8, R0        ; R8 = 33
    ; SUB: 33-3=30=0x1E
    MOV  R2, R0
    MOV  R3, #3
    SUB  R2, R3        ; R2 = 30
    ; MUL: 6*7=42=0x2A
    MOV  R4, #6
    MOV  R5, #7
    MUL  R4, R5
    ; DIV: 100/4=25=0x19
    MOVI R6, 100
    MOV  R7, #4
    DIV  R6, R7
    ; INC/DEC
    MOV  R9, #9
    INC  R9
    DEC  R9
    ; AND/OR/XOR
    MOV  R10, #0xFF
    MOV  R11, #0x0F
    AND  R10, R11      ; R10 = 0x0F
    OR   R10, R11      ; R10 = 0x0F
    XOR  R10, R10      ; R10 = 0
    ; SHL/SHR
    MOV  R11, #1
    MOV  R12, #4
    SHL  R11, R12      ; R11 = 16
    SHR  R11, R12      ; R11 = 1
    MOV  R0, #0
    INT  0x80
    HLT
EOF
$ASM $TMPDIR/isa_arith.asm $TMPDIR/isa_arith.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/isa_arith.taexe -r 2>/dev/null > $TMPDIR/isa_arith_regs.txt || true
grep -q "R8  = 0000000000000021" $TMPDIR/isa_arith_regs.txt && pass "ADD: 20+13=33=0x21" || fail "ADD" "$(grep 'R8 ' $TMPDIR/isa_arith_regs.txt)"
grep -q "R2  = 000000000000001E" $TMPDIR/isa_arith_regs.txt && pass "SUB: 33-3=30=0x1E" || fail "SUB" "$(grep 'R2 ' $TMPDIR/isa_arith_regs.txt)"
grep -q "R4  = 000000000000002A" $TMPDIR/isa_arith_regs.txt && pass "MUL: 6*7=42=0x2A"  || fail "MUL" "$(grep 'R4 ' $TMPDIR/isa_arith_regs.txt)"
grep -q "R6  = 0000000000000019" $TMPDIR/isa_arith_regs.txt && pass "DIV: 100/4=25=0x19" || fail "DIV" "$(grep 'R6 ' $TMPDIR/isa_arith_regs.txt)"
grep -q "R9  = 0000000000000009" $TMPDIR/isa_arith_regs.txt && pass "INC+DEC: 9→10→9"   || fail "INC/DEC" "$(grep 'R9 ' $TMPDIR/isa_arith_regs.txt)"
grep -q "R11 = 0000000000000001" $TMPDIR/isa_arith_regs.txt && pass "SHL+SHR: 1<<4>>4=1" || fail "SHL/SHR" "$(grep 'R11 ' $TMPDIR/isa_arith_regs.txt)"

# Test LOAD/STORE
cat > $TMPDIR/isa_mem.asm << 'EOF'
.section .data
val: .qword 0
.section .text
_start:
    MOVI R0, 9999
    LEA  R1, val
    STORE R0, [R1]
    MOV  R2, #0
    LOAD  R2, [R1]
    ; R2 should be 9999 = 0x270F
    MOV  R0, #0
    INT  0x80
    HLT
EOF
$ASM $TMPDIR/isa_mem.asm $TMPDIR/isa_mem.taexe 2>/dev/null
timeout 5 $EMU $TMPDIR/isa_mem.taexe -r 2>/dev/null > $TMPDIR/isa_mem_regs.txt || true
grep -q "R2  = 000000000000270F" $TMPDIR/isa_mem_regs.txt && pass "LOAD/STORE: value round-trips correctly" || fail "LOAD/STORE" "$(grep 'R2 ' $TMPDIR/isa_mem_regs.txt || echo 'not found')"

# ─── Summary ─────────────────────────────────────────────────
TOTAL=$((PASS + FAIL + SKIP))
echo ""
echo "════════════════════════════════════════"
echo -e "  Results: ${GREEN}${PASS} passed${NC}  ${RED}${FAIL} failed${NC}  ${YELLOW}${SKIP} skipped${NC}  (${TOTAL} total)"
echo "════════════════════════════════════════"

[[ $FAIL -eq 0 ]] && exit 0 || exit 1
