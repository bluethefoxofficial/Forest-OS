; src/context_switch.asm
; Low-level context switching for Forest OS.
; Supports both i686 (32-bit) and x86_64 (64-bit) via %ifdef.
;
; ── 32-bit calling convention (cdecl) ──────────────────────────────────────
;   Arguments are on the stack:
;     [EBP+8]  = arg1   [EBP+12] = arg2   [EBP+16] = arg3
;   Callee saves: EBX, ESI, EDI, EBP   (EAX, ECX, EDX are caller-saved)
;
; ── 64-bit calling convention (System V AMD64 ABI) ──────────────────────────
;   Arguments in registers: RDI, RSI, RDX, RCX, R8, R9
;   Callee saves: RBX, R12-R15, RBP
;
; ── IRET frame layout (32-bit, ring 0 → ring 3) ────────────────────────────
;   Pushed in reverse order so IRET pops: EIP, CS, EFLAGS, ESP, SS
;   setup_initial_cpu_state() pre-builds this exact layout on the kernel stack.
;
; ── task_switch_asm stack contract ──────────────────────────────────────────
;   When a task is first switched away from, its kernel stack holds:
;     (high address, relative to saved ESP)
;     [ESP+0 .. ESP+35] = pusha frame  (8 × 4 bytes)
;     [ESP+36]          = EFLAGS       (from pushf)
;     [ESP+40]          = saved EBP    (from push ebp / mov ebp,esp)
;     [ESP+44]          = return address (inside task_switch_asm's caller)
;   On the next switch back, popa / popf / pop ebp / ret unwind this cleanly.
; ───────────────────────────────────────────────────────────────────────────

%ifdef __x86_64__
; ============================================================================
; 64-BIT VERSION
; ============================================================================
bits 64

; ---------------------------------------------------------------------------
; void task_switch_asm(uintptr_t *old_rsp_ptr,
;                      uintptr_t  new_rsp_val,
;                      uintptr_t  new_cr3_phys)
;
; RDI = old_rsp_ptr  – where to save the current RSP (may be NULL)
; RSI = new_rsp_val  – RSP to load for the next task
; RDX = new_cr3_phys – physical address of next task's PML4
; ---------------------------------------------------------------------------
global task_switch_asm
task_switch_asm:
    ; ── Save current task's context ─────────────────────────────────────────
    push rbp
    mov  rbp, rsp

    pushfq

    ; Save all callee-saved registers (the compiler has already saved the
    ; caller-saved ones if it cared about them).  We save every GPR so the
    ; frame is symmetric and self-consistent regardless of call site.
    push rax
    push rcx
    push rdx
    push rbx
    push rbp         ; stale copy – pops into rbp after switch, harmless
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Persist args before the stack switch (they live in registers, safe).
    ; old_rsp_ptr → RDI, new_rsp_val → RSI, new_cr3 → RDX (already there).

    ; ── Save outgoing RSP ────────────────────────────────────────────────────
    test rdi, rdi
    jz   .skip_save
    mov  [rdi], rsp
.skip_save:

    ; ── Switch to incoming task ──────────────────────────────────────────────
    mov  rsp, rsi        ; load new kernel stack

    ; Switch CR3 only when the page directory actually changes.
    ; Writing the same CR3 value flushes TLB unnecessarily but is safe;
    ; a caller that passes 0 wants to skip the switch entirely.
    test rdx, rdx
    jz   .skip_cr3
    mov  cr3, rdx
.skip_cr3:

    ; ── Restore incoming task's context ─────────────────────────────────────
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    popfq

    pop rbp
    ret


; ---------------------------------------------------------------------------
; task_start_usermode_asm()
;
; Entry point for brand-new user tasks.  task_switch_asm "returns" here
; (because setup_initial_cpu_state placed this address as the return address
; on the task's kernel stack).
;
; At this point the kernel stack holds a 64-bit IRETQ frame:
;   [RSP+0]  = RIP   (ELF entry point, user virtual address)
;   [RSP+8]  = CS    (0x1B | 3 = user code selector, RPL=3)
;   [RSP+16] = RFLAGS (IF set, IOPL=0)
;   [RSP+24] = RSP   (top of user stack)
;   [RSP+32] = SS    (0x23 | 3 = user data selector, RPL=3)
;
; We load the user data selectors into DS/ES/FS/GS then IRETQ.
; ---------------------------------------------------------------------------
global task_start_usermode_asm
task_start_usermode_asm:
    ; User data selector: GDT index 4, TI=0, RPL=3  →  (4<<3)|3 = 0x23
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; SS is set by IRETQ from the frame; do not touch it here.

    iretq


; ---------------------------------------------------------------------------
; enter_usermode_asm(uint64_t entry, uint64_t user_rsp,
;                   uint64_t user_cs, uint64_t user_ds)
;
; RDI = entry     – user-space instruction pointer
; RSI = user_rsp  – user-space stack pointer
; RDX = user_cs   – user code segment selector
; RCX = user_ds   – user data segment selector
;
; Builds an IRETQ frame and drops to ring 3.
; This is the direct (non-scheduler) path; prefer task_switch_asm for tasks.
; ---------------------------------------------------------------------------
global enter_usermode_asm
enter_usermode_asm:
    ; Load user data selectors now, before we push anything sensitive.
    ; (IRETQ will set SS from the frame; we set the rest here.)
    mov  ax, cx          ; user_ds (low 16 bits of RCX)
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    ; Build the IRETQ frame on the current (kernel) stack.
    ; IRETQ pops: RIP, CS, RFLAGS, RSP, SS  (in that order, low→high addr).
    push rcx             ; SS  = user_ds
    push rsi             ; RSP = user_rsp
    pushfq               ; RFLAGS (inherit current flags)
    pop  rax
    or   rax, (1 << 9)   ; set IF – enable interrupts in user mode
    push rax
    push rdx             ; CS  = user_cs
    push rdi             ; RIP = entry point

    iretq


%else
; ============================================================================
; 32-BIT VERSION
; ============================================================================
bits 32

; ---------------------------------------------------------------------------
; void task_switch_asm(uint32_t *old_esp_ptr,
;                      uint32_t  new_esp_val,
;                      uint32_t  new_cr3_phys)
;
; [EBP+8]  = old_esp_ptr  – where to save current ESP (may be NULL / 0)
; [EBP+12] = new_esp_val  – ESP to load for the next task
; [EBP+16] = new_cr3_phys – physical address of next task's page directory
;                           (pass 0 to skip CR3 switch)
;
; Stack frame after prologue / pushes:
;   EBP+0  → saved EBP
;   EBP+4  → return address
;   EBP+8  → old_esp_ptr
;   EBP+12 → new_esp_val
;   EBP+16 → new_cr3_phys
;
; ESP at the point of saving = EBP − (4 flags + 8×4 pusha) = EBP − 36.
; That saved value is stored into *old_esp_ptr.
; When this task is resumed, popa/popf/pop ebp/ret unwind the frame exactly.
; ---------------------------------------------------------------------------
global task_switch_asm
task_switch_asm:
    push ebp
    mov  ebp, esp

    pushf
    pusha                ; saves EAX ECX EDX EBX ESP EBP ESI EDI (8 × 4 = 32 bytes)
                         ; NOTE: POPA ignores the pushed ESP slot; we rely on the
                         ; actual pop sequence, not the value in that slot.

    ; Read all three arguments BEFORE touching ESP.  Use caller-saved
    ; registers that pusha already preserved on the stack.
    mov  eax, [ebp + 8]   ; old_esp_ptr
    mov  ecx, [ebp + 12]  ; new_esp_val
    mov  edx, [ebp + 16]  ; new_cr3_phys

    ; ── Save outgoing task's kernel stack pointer ────────────────────────────
    test eax, eax
    jz   .skip_save
    mov  [eax], esp       ; store current ESP into prev_task->kernel_stack
.skip_save:

    ; ── Switch to incoming task's kernel stack ───────────────────────────────
    mov  esp, ecx         ; now running on the new task's stack

    ; ── Switch page directory (TLB flush) ────────────────────────────────────
    ; Skip if new_cr3_phys == 0 (kernel-only switch or same address space).
    test edx, edx
    jz   .skip_cr3
    mov  cr3, edx
.skip_cr3:

    ; ── Restore incoming task's context ─────────────────────────────────────
    ; At this point ESP points to whatever was saved when that task last called
    ; task_switch_asm (or to the initial frame built by setup_initial_cpu_state).
    popa
    popf
    pop  ebp
    ret


; ---------------------------------------------------------------------------
; jump_to_usermode_asm — PERMANENTLY DEPRECATED
;
; All ring-3 entry MUST go through:
;   task_switch_asm  →  (ret to)  task_start_usermode_asm  →  IRET
;
; This stub is kept only for link-time compatibility.  Reaching it at
; runtime indicates a serious scheduler bug; we halt immediately.
; ---------------------------------------------------------------------------
global jump_to_usermode_asm
jump_to_usermode_asm:
    cli
    hlt
.freeze:
    jmp  .freeze          ; if NMI resumes us, loop rather than executing garbage


; ---------------------------------------------------------------------------
; task_start_usermode_asm()
;
; task_switch_asm's "ret" lands here for new user tasks because
; setup_initial_cpu_state() placed this address at the top of the
; pre-built kernel stack frame (as the return address).
;
; Stack layout on entry (built by setup_initial_cpu_state):
;   [ESP+0]  = EIP    – ELF entry point (user virtual address)
;   [ESP+4]  = CS     – 0x1B (user code selector, RPL=3)
;   [ESP+8]  = EFLAGS – 0x202 (IF=1, reserved bit 1)
;   [ESP+12] = ESP    – top of user-mode stack
;   [ESP+16] = SS     – 0x23 (user data selector, RPL=3)
;
; CRITICAL: Do NOT push or pop anything before IRET.
;           Any stack modification shifts the frame and causes IRET to
;           consume wrong values, jumping to a garbage address in ring 3.
;
; The serial debug code that was previously here did exactly this —
; it pushed EAX+EDX (−8 bytes) and never popped them before IRET,
; corrupting the frame.  It has been removed.
; ---------------------------------------------------------------------------
global task_start_usermode_asm
task_start_usermode_asm:
    ; Set user data selectors.  SS is handled by IRET from the frame above.
    ; Selector 0x23 = GDT index 4, TI=0 (GDT), RPL=3.
    mov  ax, 0x23
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    ; Transfer to ring 3.  IRET pops EIP, CS, EFLAGS, ESP, SS in order.
    iret


%endif
