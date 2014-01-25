;******************************************************************************
;* add_bitmaps.asm: SSE2 and x86 add_bitmaps
;******************************************************************************

%include "x86inc.asm"

SECTION .text

;------------------------------------------------------------------------------
; void get_cpuid( uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx);
;------------------------------------------------------------------------------

INIT_XMM
cglobal get_cpuid, 4, 5, 0
    push rbx
    push r3
    push r2
    push r1
    push r0
    mov eax, [r0]
    xor ecx, ecx
    cpuid
    pop r4
    mov [r4], eax
    pop r4
    mov [r4], ebx
    pop r4
    mov [r4], ecx
    pop r4
    mov [r4], edx
    pop rbx
    RET
