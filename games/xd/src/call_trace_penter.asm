; Call trace (testing build only: cmake -DXD_CALL_TRACE=ON, see call_trace.cpp).
;
; With /Gh every game function calls _penter near its start (x64 MSVC: after its prologue), before it
; uses its arguments, so all of the function's argument registers are still live here: everything the
; Windows x64 convention lets a called function change (rax, rcx, rdx, r8-r11, xmm0-xmm5) is saved and
; put back. The stack's alignment here differs between functions, so it is realigned.
;
; The FLAGS register is kept too (pushfq/popfq), also when the trace is off. MSVC treats the _penter call
; as changing nothing and schedules flag tests across it, e.g. GSboundSetAnimFrame:
;     test byte ptr [rcx],8 / movaps [rsp+20h],xmm6 / call _penter / movaps xmm6,xmm1 / je ...
; Until 2026-11-07 _penter changed the flags ("cmp gPortCallTraceOn, 0" even with the trace off), so in
; every call-trace build such functions took the wrong branch: with the trace off GSboundSetAnimFrame never
; stored its frame, with it on the game crashed there a few frames later (wrong branch -> garbage index).
;
; Coverage only (XD_CALL_COVERAGE without XD_CALL_TRACE: gPortCoverOnly = 1): the function is added to
; gPortCoverTable here, without a call (registers parked in globals under a spin lock), and its
; "call _penter" is then patched out so it runs at full speed from its second call on.

EXTERN PortCallTrace_Record:PROC
EXTERN gPortCallTraceOn:BYTE
EXTERN gPortCoverOnly:BYTE
EXTERN gPortCoverTable:QWORD
EXTERN gPortCoverCount:DWORD
EXTERN gPortCoverCodeLo:QWORD
EXTERN gPortCoverCodeHi:QWORD

COVER_MASK EQU 131071                ; kCoverSlots - 1 (call_trace.cpp)

.data
ALIGN 16
coverLock  DD 0
ALIGN 8
coverRax   DQ 0
coverRcx   DQ 0
coverRdx   DQ 0
coverR8    DQ 0

.code

; Stack inside: [rbp] old rbp, [rbp+8] the traced function's flags, [rbp+16] where _penter returns to (just
; after the call, in the traced function), rbp+24 = the traced function's stack pointer.
_penter PROC FRAME
    pushfq
    .allocstack 8
    push    rbp
    .pushreg rbp
    mov     rbp, rsp
    .setframe rbp, 0
    .endprolog
    cmp     byte ptr [gPortCallTraceOn], 0
    jne     trace_on
done:
    mov     rsp, rbp
    pop     rbp
    popfq
    ret
trace_on:
    cmp     byte ptr [gPortCoverOnly], 0
    je      trace_call
cover_lock:
    lock bts dword ptr [coverLock], 0
    jc      cover_spin
    mov     [coverRax], rax
    mov     [coverRcx], rcx
    mov     [coverRdx], rdx
    mov     rax, [rbp + 16]          ; inside the traced function
    mov     rcx, 9E3779B97F4A7C15h
    imul    rcx, rax
    shr     rcx, 47                  ; 17 bits: a slot number
cover_probe:
    lea     rdx, gPortCoverTable
    mov     rdx, [rdx + rcx*8]
    cmp     rdx, rax
    je      cover_done
    test    rdx, rdx
    jz      cover_insert
    inc     rcx
    and     rcx, COVER_MASK
    jmp     cover_probe
cover_insert:
    lea     rdx, gPortCoverTable
    mov     [rdx + rcx*8], rax
    lock inc dword ptr [gPortCoverCount]
    ; Recorded once is enough: turn this function's "call _penter" (5 bytes: E8 rel32, just before the
    ; return address) into "jmp +3" (EB 03), so from now on it runs at full speed. Only inside the game code
    ; block (made writable by call_trace.cpp) and when the 2 bytes don't cross a cache line.
    lea     rcx, [rax - 5]
    cmp     rcx, [gPortCoverCodeLo]
    jb      cover_done
    cmp     rcx, [gPortCoverCodeHi]
    jae     cover_done
    cmp     byte ptr [rcx], 0E8h
    jne     cover_done
    movsxd  rdx, dword ptr [rcx + 1]
    add     rdx, rax                 ; the call's target
    mov     [coverR8], r8
    lea     r8, _penter
    cmp     rdx, r8
    mov     r8, [coverR8]
    jne     cover_done
    mov     rdx, rcx
    and     rdx, 63
    cmp     rdx, 63
    je      cover_done
    mov     word ptr [rcx], 03EBh
cover_done:
    mov     rax, [coverRax]
    mov     rcx, [coverRcx]
    mov     rdx, [coverRdx]
    mov     dword ptr [coverLock], 0
    jmp     done
cover_spin:
    pause
    test    dword ptr [coverLock], 1
    jnz     cover_spin
    jmp     cover_lock
trace_call:
    and     rsp, -16
    sub     rsp, 192                 ; 32 shadow space, 7 registers (64), 6 xmm registers (96)
    mov     [rsp + 32], rax
    mov     [rsp + 40], rcx
    mov     [rsp + 48], rdx
    mov     [rsp + 56], r8
    mov     [rsp + 64], r9
    mov     [rsp + 72], r10
    mov     [rsp + 80], r11
    movaps  [rsp + 96], xmm0
    movaps  [rsp + 112], xmm1
    movaps  [rsp + 128], xmm2
    movaps  [rsp + 144], xmm3
    movaps  [rsp + 160], xmm4
    movaps  [rsp + 176], xmm5
    mov     rcx, [rbp + 16]          ; where _penter returns to: just after the call, in the traced function
    lea     rdx, [rbp + 24]          ; the traced function's stack pointer
    lea     r8, [rsp + 32]           ; the saved registers: rax rcx rdx r8 r9 r10 r11, pad, xmm0-xmm5 (func_cost.cpp)
    call    PortCallTrace_Record
    mov     rax, [rsp + 32]
    mov     rcx, [rsp + 40]
    mov     rdx, [rsp + 48]
    mov     r8, [rsp + 56]
    mov     r9, [rsp + 64]
    mov     r10, [rsp + 72]
    mov     r11, [rsp + 80]
    movaps  xmm0, [rsp + 96]
    movaps  xmm1, [rsp + 112]
    movaps  xmm2, [rsp + 128]
    movaps  xmm3, [rsp + 144]
    movaps  xmm4, [rsp + 160]
    movaps  xmm5, [rsp + 176]
    jmp     done
_penter ENDP

END
