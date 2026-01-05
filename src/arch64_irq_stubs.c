#include "include/interrupt.h"

#if ARCH_64BIT && defined(ENABLE_C_INTERRUPT_STUBS_FALLBACK)
/* Fallback C stubs for environments where the assembly stubs are unavailable.
 * Normal kernel builds rely on interrupt_stubs.s, so these stay disabled. */
void syscall_handler(void) {}
void spurious_irq_handler(void) {}
void double_fault_handler(void) {}
void nmi_handler(void) {}
void machine_check_handler(void) {}

void irq_stub_0(void) {}
void irq_stub_1(void) {}
void irq_stub_2(void) {}
void irq_stub_3(void) {}
void irq_stub_4(void) {}
void irq_stub_5(void) {}
void irq_stub_6(void) {}
void irq_stub_7(void) {}
void irq_stub_8(void) {}
void irq_stub_9(void) {}
void irq_stub_10(void) {}
void irq_stub_11(void) {}
void irq_stub_12(void) {}
void irq_stub_13(void) {}
void irq_stub_14(void) {}
void irq_stub_15(void) {}
#endif
