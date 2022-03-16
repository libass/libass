;******************************************************************************
;* cpuid.asm: check for cpu capabilities
;******************************************************************************
;* Copyright (C) 2013 rcombs <rcombs@rcombs.me>
;*
;* This file is part of libass.
;*
;* Permission to use, copy, modify, and distribute this software for any
;* purpose with or without fee is hereby granted, provided that the above
;* copyright notice and this permission notice appear in all copies.
;*
;* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
;* WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
;* MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
;* ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
;* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
;* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
;* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
;******************************************************************************

%include "x86/x86inc.asm"

SECTION .text

;------------------------------------------------------------------------------
; uint32_t has_cpuid( void );
;------------------------------------------------------------------------------

INIT_XMM
cglobal has_cpuid, 0, 0, 0
%if ARCH_X86_64
    mov eax, 1
%else
    pushfd
    pop ecx
    mov eax, ecx
    xor eax, 0x00200000
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    and eax, 0x00200000 ; non-zero if bit is writable
    push ecx            ; Restore original EFLAGS
    popfd
%endif
    RET

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

;-----------------------------------------------------------------------------
; void get_xgetbv( uint32_t op, uint32_t *eax, uint32_t *edx )
;-----------------------------------------------------------------------------

INIT_XMM
cglobal get_xgetbv, 3, 7, 0
    push  r2
    push  r1
    mov  ecx, r0d
    xgetbv
    pop   r4
    mov [r4], eax
    pop   r4
    mov [r4], edx
    RET
