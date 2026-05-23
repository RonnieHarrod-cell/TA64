; ============================================================
;  hello.asm — TA64 Hello World
;  Demonstrates: sections, strings, syscalls
; ============================================================

.section .data

hello_str:
    .string "Hello from TA64!\n"
hello_len:
    .word 17

.section .text

_start:
    ; SYS_WRITE(fd=1, buf=hello_str, len=17)
    MOV  R0, #1          ; syscall = SYS_WRITE
    MOV  R1, #1          ; fd = stdout
    LEA  R2, hello_str   ; buffer address
    MOVI R3, 17          ; length
    INT  0x80

    ; SYS_EXIT(0)
    MOV  R0, #0
    INT  0x80
    HLT
