#ifndef INTERRUPT_DMA_COMPLETION_H
#define INTERRUPT_DMA_COMPLETION_H

#include <stdint.h>
#include <stdbool.h>

int interrupt_dma_completion_init(void);
bool interrupt_dma_completion_is_initialized(void);

#endif
