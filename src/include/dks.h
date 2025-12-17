#ifndef DKS_H
#define DKS_H

#include "types.h"

// Launch the Direct Kernel Shell (DKS) interactive prompt.
// This is a minimal in-kernel shell used when the userspace shell fails
// to load so that we can still inspect and poke the system.
void dks_run(void);

#endif
