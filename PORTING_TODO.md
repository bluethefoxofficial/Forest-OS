## Forest OS Advanced Interrupt/ACPI Port

This repository currently mixes the legacy interrupt/memory architecture with a new, much more feature-rich design (see the numerous `interrupt_*`, `acpi_*`, `mm_*`, `timer_*`, APIC/IO-APIC/SMP files). The headers have already been rewritten for the new world, but most compiled C sources are still the old implementations. The tree does not compile: the new headers declare APIs (`struct interrupt_context`, `debuglog_printf`, timer abstractions, IO‑APIC helpers, etc.) that have no matching implementations, and the old code still uses the legacy ISR signatures and PIC-only paths.

Below is a structured task list for the next AI to complete the port. Every bullet needs real code written or removed so the kernel builds and links cleanly.

### 1. Core Interrupt/IDT Infrastructure
1. **Unify handler signature**  
   - Replace the legacy ISR interface (`void handler(struct interrupt_frame*, uint32)`) with `irq_return_t handler(int vector, struct interrupt_context *ctx)` everywhere (`interrupt.c`, device drivers, timers, PS/2, etc.).  
   - Ensure `struct interrupt_context` (from `interrupt.h`) is fully populated by the interrupt entry stubs.
2. **Implement the new interrupt manager**  
   - Provide a definition for `struct interrupt_manager interrupt_mgr` and all functions declared in `interrupt.h` (registration, vector allocation, priority, statistics, tracing, etc.).  
   - Wire `interrupt_common_handler`, `interrupt_enter_nested`, `interrupt_exit_nested`, tracing, and per-CPU context stacks to the new APIs.
3. **IDT and stubs**  
   - Use `src/interrupt_stubs.s` for ISR entry on both x86-32 and x86-64 builds.  
   - Update `idt.c` to call `debuglog_printf`, `panic`, `get_kernel_cs`, and to register handlers via the new manager interface.  
   - Remove the placeholder helper in `interrupt_utils.c` once the real manager is in place.

### 2. Exception Handling & Logging
4. **Exceptions**  
   - Port `exceptions.c` to the new handler type, including statistics, fatal handling, and signal delivery (`task_terminate_current`).  
   - Make sure all `debuglog_printf`/`panic` calls resolve.
5. **Debug subsystem**  
   - Expose `debuglog_printf` (variadic) or replace all usages with existing logging helpers.  
   - Confirm `debug.c` + `debuglog.c` cooperate (initialization, level control, etc.).

### 3. APIC / IO-APIC / SMP / ACPI
6. **APIC bring-up**  
   - Finish `apic.c`, `local_apic.h`, and `apic_timer.c`, ensuring `mm_map_physical_page` maps LAPIC registers.  
   - Provide `debuglog_printf`, `panic`, and other dependencies used inside APIC code.
7. **IO-APIC**  
   - Implement all helper APIs declared in `include/io_apic.h` and used across `acpi_interrupt_routing`, SMP distribution, MSI, etc.  
   - Remove duplicated ACPI MADT structs (the new headers already define them).
8. **ACPI interrupt routing**  
   - Wire `acpi_interrupt_routing.c` so it feeds IO-APIC/MADT data into the interrupt controller modules.  
   - Ensure `acpi_root_pointer`, `find_acpi_table`, and ACPI init functions exist in `acpi.c`.
9. **SMP**  
   - Complete `smp.c`, `ipi_smp_coordination.c`, `smp_interrupt_distribution.c`, including CPU discovery, IPIs, and load balancing hooks.  
   - Implement any referenced helpers in `smp.h` (`smp_get_current_cpu`, etc.).

### 4. Timer Abstraction + HPET/APIC/PIT/TSC
10. **Timer sources**  
    - Finalize `timer_abstraction.c`, hooking up HPET (`hpet.c`), APIC timer (`apic_timer.c`), and PIT (`pit.c`) so they register via `register_timer_source`.  
    - Implement the full timer abstraction API (`timer_abstraction_create_timer`, `timer_abstraction_start_timer`, `timer_get_frequency`, etc.).
11. **TSC calibration**  
    - Ensure `tsc_calibration.c` compiles: provide `cpuid`, `apic_timer_is_available`, HPET helpers, and `cpu_relax`.  
12. **Legacy timer glue**  
    - Update `timer.c`/`timer_calibration.c` to use the new infrastructure and remove direct PIC-only assumptions.

### 5. Device Drivers & PIC compatibility
13. **PS/2 keyboard/mouse**  
    - Port `ps2_keyboard.c`, `ps2_mouse.c`, `keyboard_interrupt_handler.c`, `mouse_interrupt_handler.c`, and `timer.c` to the new handler signature and registration APIs.
14. **PIC fallback**  
    - Keep minimal PIC init in `pic_8259a.c` for early boot, but disable it when IO-APIC is active. Update the file to compile against the new headers (it currently duplicates `idt_register_handler`, etc.).
15. **Interrupt-driven IO / MSI / Watchdog**  
    - Ensure modules like `interrupt_driven_io.c`, `msi_support.c`, `watchdog_interrupt_support.c`, `interrupt_coalescing.c`, etc., can compile (requires timer abstraction + interrupt manager).

### 6. Memory subsystem alignment
16. **Inline helpers**  
    - Provide implementations for the `static inline` functions declared in `mm.h` (`page_to_pfn`, `pfn_to_page`, `page_address`, etc.), either inline or via `mm_buddy.c`.
17. **`mm_allocate_pages` and variants**  
    - Ensure the new memory APIs referenced by `fault_prevention.c`, interrupts, and other modules exist (buddy allocator, slab, page cache).
18. **`mm_map_physical_page`**  
    - Implement a real mapping helper (even identity-mapped) in `cpu_utils.c` or `mm_*` modules so APIC/HPET can access physical registers.

### 7. Build System Cleanup
19. **Makefile sanity**  
    - Remove the legacy `makefile` and ensure the new `Makefile` only lists the modern sources. Double-check `ALL_OBJECTS` references so no deleted files are pulled in.  
    - Keep helper scripts (`build-helper.sh`, `build-config.mk`, etc.) consistent with the new structure.
20. **Incremental builds**  
    - After each subsystem is ported, run `make all -j` and fix compile/link errors before moving on. Expect multiple iterations.

### Notes
- Many new modules (`interrupt_priority.c`, `interrupt_statistics.c`, `interrupt_registration.c`, `interrupt_stack_switching.c`, etc.) have deep dependencies. They can only compile after the core interrupt manager, timer abstraction, and memory helpers exist. Please prioritize foundation work first.
- Remove obsolete files once their functionality is replaced (legacy `interrupt.c`, `interrupt_handlers.c`, duplicated ACPI structs, etc.) to avoid confusion.
- Keep logging consistent (`debuglog_printf`, `debug_print`, etc.) and ensure headers don’t declare nonexistent functions.

Once these steps are completed, the tree should compile using the advanced infrastructure. Feel free to reorder tasks if dependencies dictate it, but don’t skip the fundamentals (interrupt manager, APIC/IO-APIC, timer abstraction, memory helpers), because every other subsystem relies on them.
