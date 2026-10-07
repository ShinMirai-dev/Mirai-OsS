#include "irq.h"
#include <stddef.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    asm volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Must match the register push order in irq_common_stub (isr.asm) */
struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

volatile uint32_t timer_ticks = 0;

/* US scancode set 1 -> ASCII (unshifted) */
static const char scancode_ascii[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',
};

#define KBD_BUF_SIZE 256
static char kbd_buffer[KBD_BUF_SIZE];
static volatile size_t kbd_head = 0, kbd_tail = 0;

static void keyboard_push(char c) {
    size_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) {          /* drop the key if the buffer is full */
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
}

char keyboard_read_blocking(void) {
    while (kbd_tail == kbd_head) {
        asm volatile ("hlt");        /* sleep the CPU until the next interrupt */
    }
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}

/* Called from irq_common_stub in isr.asm for every hardware interrupt */
void irq_handler(struct registers regs) {
    uint32_t irq = regs.int_no - 32;

    if (irq == 0) {
        timer_ticks++;
    } else if (irq == 1) {
        uint8_t sc = inb(0x60);
        if (sc < 128) {
            char c = scancode_ascii[sc];
            if (c) keyboard_push(c);
        }
    }

    if (irq >= 8) outb(0xA0, 0x20);  /* EOI to slave PIC for IRQ8-15 */
    outb(0x20, 0x20);                 /* EOI to master PIC, always */
}

void irq_init(void) {
    /* Configure the PIT (Programmable Interval Timer) to fire IRQ0 at ~100Hz.
       PIT input clock is ~1193182 Hz; divisor = clock / desired_frequency. */
    uint32_t divisor = 1193182 / 100;
    outb(0x43, 0x36);                       /* channel 0, mode 3 (square wave), binary */
    outb(0x40, divisor & 0xFF);              /* low byte */
    outb(0x40, (divisor >> 8) & 0xFF);       /* high byte */
}
