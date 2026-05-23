; ============================================================
;  sort_demo.asm — TA64 Bubble Sort Demo
;
;  Sorts an array of 8 integers stored in .data,
;  then prints "sorted\n" via SYS_WRITE.
;
;  Demonstrates: loops, memory addressing with offsets,
;  LOAD/STORE patterns, conditional swaps.
; ============================================================

.section .data

; The array: 8 x 64-bit values (8 bytes each)
array:
    .qword 42
    .qword 7
    .qword 19
    .qword 3
    .qword 88
    .qword 55
    .qword 1
    .qword 23

msg_sorted:
    .string "Sorted!\n"

.section .text

; ── _start ───────────────────────────────────────────────────
_start:
    ; R0 = pointer to array
    LEA  R0, array
    ; R1 = length = 8
    MOV  R1, #8
    CALL bubble_sort

    ; Print "Sorted!\n"
    MOV  R0, #1          ; SYS_WRITE
    MOV  R1, #1          ; fd = stdout
    LEA  R2, msg_sorted
    MOV  R3, #8
    INT  0x80

    ; Exit
    MOV  R0, #0
    INT  0x80
    HLT

; ── bubble_sort(R0=array_ptr, R1=length) ─────────────────────
; Classic O(n²) bubble sort — ascending order.
; Uses: R0=base, R1=n, R2=i, R3=j, R4=a[j], R5=a[j+1]
; ─────────────────────────────────────────────────────────────
bubble_sort:
    PUSH R13
    MOV  R13, SP
    PUSH R0              ; save base
    PUSH R1              ; save n

    MOV  R2, #0          ; outer i = 0

.outer_loop:
    ; Load saved n
    LOAD R1, [R13 + -16]
    CMP  R2, R1
    JGE  .sort_done

    MOV  R3, #0          ; inner j = 0

.inner_loop:
    ; limit = n - 1 - i
    LOAD R1, [R13 + -16]
    DEC  R1
    SUB  R1, R2          ; R1 = n-1-i
    CMP  R3, R1
    JGE  .next_outer

    ; Load a[j]   = base + j*8
    LOAD R0, [R13 + -8]  ; restore base
    MOV  R6, #8
    MOV  R7, R3
    MUL  R7, R6          ; R7 = j*8
    ADD  R0, R7          ; R0 = &a[j]
    LOAD R4, [R0]        ; R4 = a[j]

    ; Load a[j+1] = base + (j+1)*8
    MOV  R8, R3
    INC  R8
    MUL  R8, R6          ; R8 = (j+1)*8
    LOAD R0, [R13 + -8]
    ADD  R0, R8          ; R0 = &a[j+1]
    LOAD R5, [R0]        ; R5 = a[j+1]

    ; if a[j] <= a[j+1]: no swap
    CMP  R4, R5
    JLE  .no_swap

    ; Swap a[j] and a[j+1]
    LOAD R0, [R13 + -8]
    ADD  R0, R7          ; R0 = &a[j]
    STORE R5, [R0]       ; a[j] = a[j+1]
    LOAD R0, [R13 + -8]
    ADD  R0, R8          ; R0 = &a[j+1]
    STORE R4, [R0]       ; a[j+1] = a[j]

.no_swap:
    INC  R3
    JMP  .inner_loop

.next_outer:
    INC  R2
    JMP  .outer_loop

.sort_done:
    MOV  SP, R13
    POP  R13
    RET
