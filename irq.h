#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

extern volatile uint32_t timer_ticks;

void irq_init(void);
char keyboard_read_blocking(void);

#endif
