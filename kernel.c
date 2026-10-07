#include <stdint.h>
#include <stddef.h>
#include "gdt.h"
#include "idt.h"
#include "irq.h"

/* --- Port I/O --- */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    asm volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* --- VGA text mode --- */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
static uint16_t* const VGA_BUFFER = (uint16_t*) 0xB8000;
static size_t row = 0, col = 0;
static uint8_t color = 0x0A; /* light green on black */

static inline uint16_t vga_entry(char c, uint8_t clr) {
    return (uint16_t) c | (uint16_t) clr << 8;
}

void terminal_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_BUFFER[y * VGA_WIDTH + x] = vga_entry(' ', color);
    row = 0; col = 0;
}

void scroll(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_BUFFER[(y-1) * VGA_WIDTH + x] = VGA_BUFFER[y * VGA_WIDTH + x];
    for (size_t x = 0; x < VGA_WIDTH; x++)
        VGA_BUFFER[(VGA_HEIGHT-1) * VGA_WIDTH + x] = vga_entry(' ', color);
    row = VGA_HEIGHT - 1;
}

void putchar(char c) {
    if (c == '\n') {
        col = 0; row++;
    } else if (c == '\b') {
        if (col > 0) { col--; VGA_BUFFER[row * VGA_WIDTH + col] = vga_entry(' ', color); }
    } else {
        VGA_BUFFER[row * VGA_WIDTH + col] = vga_entry(c, color);
        col++;
        if (col >= VGA_WIDTH) { col = 0; row++; }
    }
    if (row >= VGA_HEIGHT) scroll();
}

void print(const char* s) {
    for (size_t i = 0; s[i] != '\0'; i++) putchar(s[i]);
}

/* Keyboard input now arrives via IRQ1 (see irq.c); keyboard_read_blocking()
   sleeps the CPU with hlt until a key is pushed into the buffer by the
   interrupt handler. */

/* --- Power control --- */
void do_reboot(void) {
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
}

void do_shutdown(void) {
    print("\nShutting down (QEMU only)...\n");
    outw(0x604, 0x2000); /* QEMU ACPI shutdown port */
    /* If this doesn't work (e.g. real hardware later), just halt */
    asm volatile ("cli");
    for (;;) asm volatile ("hlt");
}

/* --- Simple string compare --- */
int streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

/* --- Shell --- */
void kernel_main(void) {
    gdt_init();
    idt_init();
    irq_init();
    asm volatile ("sti");   /* enable interrupts now that the IDT + PIC are set up */
    terminal_clear();
    print("========================================\n");
    print("          SHINMIRAI OS v0.4\n");
    print("========================================\n\n");
    print("Booting ShinMirai...\n\n");
    print("[ OK ] CPU initialized\n");
    print("[ OK ] GDT initialized\n");
    print("[ OK ] IDT initialized\n");
    print("[ OK ] Interrupts enabled\n");
    print("[ OK ] Memory initialized\n");
    print("[ OK ] Display initialized\n");
    print("[ OK ] Keyboard initialized (interrupt-driven)\n\n");

    char buf[128];
    for (;;) {
        print("shinmirai> ");
        size_t len = 0;
        for (;;) {
            char c = keyboard_read_blocking();
            if (c == '\n') { putchar('\n'); buf[len] = '\0'; break; }
            if (c == '\b') {
                if (len > 0) { len--; putchar('\b'); }
                continue;
            }
            if (len < sizeof(buf) - 1) { buf[len++] = c; putchar(c); }
        }

        if (len == 0) continue;
        else if (streq(buf, "help"))
            print("Commands: help clear echo sysinfo uptime reboot shutdown test\n");
        else if (streq(buf, "clear"))
            terminal_clear();
        else if (buf[0]=='e'&&buf[1]=='c'&&buf[2]=='h'&&buf[3]=='o'&&buf[4]==' ')
            { print(buf + 5); print("\n"); }
        else if (streq(buf, "sysinfo"))
            print("ShinMirai OS v0.4 - x86 (32-bit) - QEMU test build\n");
        else if (streq(buf, "uptime")) {
            print("Timer ticks since boot: ");
            char numbuf[12];
            uint32_t n = timer_ticks;
            int i = 0;
            if (n == 0) { numbuf[i++] = '0'; }
            while (n > 0) { numbuf[i++] = '0' + (n % 10); n /= 10; }
            while (i > 0) putchar(numbuf[--i]);
            print("\n");
        }
        else if (streq(buf, "reboot"))
            do_reboot();
        else if (streq(buf, "shutdown"))
            do_shutdown();
        else if (streq(buf, "test")) {
            print("Triggering divide-by-zero exception...\n");
            volatile int a = 10, b = 0;
            volatile int c = a / b;   /* deliberately crash into isr0 */
            (void)c;
        }
        else
            print("Unknown command. Type 'help'.\n");
    }
}
