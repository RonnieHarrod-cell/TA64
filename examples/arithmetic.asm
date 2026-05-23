; ============================================================
;  arithmetic.asm — TA64 Arithmetic Demo
;  Tests: ADD, SUB, MUL, DIV, MOD, INC, DEC, bitwise ops
; ============================================================

.section .text

_start:
    ; ── Basic arithmetic ──────────────────────────────────
    MOV  R0, #20         ; R0 = 20
    MOV  R1, #7          ; R1 = 7

    ADD  R0, R1          ; R0 = 27
    SUB  R0, R1          ; R0 = 20
    MUL  R0, R1          ; R0 = 140
    DIV  R0, R1          ; R0 = 20
    INC  R0              ; R0 = 21
    DEC  R0              ; R0 = 20

    ; ── Bitwise ops ───────────────────────────────────────
    MOV  R2, #0xFF       ; R2 = 255
    MOV  R3, #0x0F       ; R3 = 15
    AND  R2, R3          ; R2 = 0x0F
    OR   R2, R3          ; R2 = 0x0F
    XOR  R2, R3          ; R2 = 0

    ; ── Shift operations ──────────────────────────────────
    MOV  R4, #1
    MOV  R5, #4
    SHL  R4, R5          ; R4 = 16

    ; ── Function call test ────────────────────────────────
    MOV  R0, #10
    MOV  R1, #32
    CALL add_two
    ; R0 = 42

    ; ── Exit ──────────────────────────────────────────────
    MOV  R0, #0
    INT  0x80
    HLT

; ── add_two(R0, R1) → R0 ─────────────────────────────────
add_two:
    ADD  R0, R1
    RET

; ── fibonacci(n in R0) → R0 ──────────────────────────────
;  Iterative: compute F(n)
fibonacci:
    PUSH R13
    MOV  R13, SP
    CMP  R0, R0           ; ZF only if R0=0
    MOVI R1, 0
    CMP  R0, R1
    JE   .fib_zero
    MOVI R1, 1
    CMP  R0, R1
    JLE  .fib_one

    MOV  R3, #0           ; a = 0
    MOV  R4, #1           ; b = 1
    MOV  R5, #2           ; i = 2
.fib_loop:
    CMP  R5, R0
    JGT  .fib_done
    MOV  R6, R4           ; temp = b
    ADD  R4, R3           ; b = a + b
    MOV  R3, R6           ; a = temp
    INC  R5
    JMP  .fib_loop
.fib_done:
    MOV  R0, R4
    MOV  SP, R13
    POP  R13
    RET
.fib_zero:
    MOV  R0, #0
    MOV  SP, R13
    POP  R13
    RET
.fib_one:
    MOV  SP, R13
    POP  R13
    RET
