extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);

#define UART_BASE 0x10000000UL
#define UART_RBR  (unsigned char*)(UART_BASE + 0x0)
#define UART_THR  (unsigned char*)(UART_BASE + 0x0)
#define UART_IER  (unsigned char*)(UART_BASE + 0x1)
#define UART_IIR  (unsigned char*)(UART_BASE + 0x2)
#define UART_MCR  (unsigned char*)(UART_BASE + 0x4)
#define UART_LSR  (unsigned char*)(UART_BASE + 0x5)
#define LSR_DR    (1 << 0)
#define LSR_TDRQ  (1 << 5)
#define UART_IRQ  0x0a

// PLIC 是一個 MMIO 裝置 要透過固定的硬體位址去讀寫
#define PLIC_BASE            0xc000000UL
// 後面有加一個括號代表不是一般函式，而是一個會帶參數的巨集 eg. PLIC_PRIORITY(0)
#define PLIC_PRIORITY(irq)   (PLIC_BASE + (irq) * 4)
#define PLIC_ENABLE(hart)    (PLIC_BASE + 0x002080 + (hart) * 0x0100)
#define PLIC_THRESHOLD(hart) (PLIC_BASE + 0x201000 + (hart) * 0x2000)
#define PLIC_CLAIM(hart)     (PLIC_BASE + 0x201004 + (hart) * 0x2000)

unsigned long boot_cpu_hartid = 0;

// 3
void uart_init() {
    volatile unsigned char *ier = UART_IER;
    volatile unsigned char *mcr = UART_MCR;

    // Enable RX interrupt.
    *ier |= 0x01;

    // Allow UART interrupt output to PLIC (OUT2 bit).
    *mcr |= (1U << 3);
}

// 在 S-mode 開啟 Global Interrupt. (SIE = Supervisor Interrupt Enable)
void irq_enable() {
    asm volatile("csrsi sstatus, (1 << 1)");
}

// 1
void plic_init() {
    // TODO: Implement this function
    // (1) Set UART interrupt priority
    // 跟硬體裝置有關的記憶體位址 用 volatile
    volatile unsigned int *priority = (volatile unsigned int *)PLIC_PRIORITY(UART_IRQ);
    volatile unsigned int *enable = (volatile unsigned int *)PLIC_ENABLE(boot_cpu_hartid);
    volatile unsigned int *threshold = (volatile unsigned int *)PLIC_THRESHOLD(boot_cpu_hartid);
    
    // (2) Set UART interrupt enable for the boot hart
    // UART IRQ priority > 0 才會被 PLIC 視為有效中斷來源。
    *priority = 1;

    // (3) Set threshold for the boot hart
    // 為目前 boot hart 啟用 UART IRQ。表示這個 hart 願意接收 UART 的中斷
    *enable |= (1U << UART_IRQ);

    // threshold 設 0，讓所有 priority > 0 的中斷都可以送進來。
    *threshold = 0;

    // (4) Enable external interrupts
    // 開啟 supervisor external interrupt (SEIE)。
    // 把 sie 這個 CSR 裡的第 9 bit 設成 1，也就是 SEIE。
    // 也就是像 UART 這種透過 PLIC 送來的中斷，CPU 才會真的接收到
    // 讓 外部中斷 可以進到 S-mode
    enable_external_interrupt();
}

// 2
// 在中斷處理時從 PLIC 讀出 pending IRQ（claim）。
int plic_claim() {
    volatile unsigned int *claim = (volatile unsigned int *)PLIC_CLAIM(boot_cpu_hartid);
    return (int)(*claim);
}

// 2
// 在處理完 IRQ 後，把 IRQ 編號寫回 PLIC 的 
// complete register 以解鎖該來源（complete）。
void plic_complete(int irq) {
    volatile unsigned int *claim = (volatile unsigned int *)PLIC_CLAIM(boot_cpu_hartid);
    *claim = (unsigned int)irq;
}

// 5
void do_trap() {
    int irq = plic_claim();
    if (irq == UART_IRQ) {
        char c = *UART_RBR;
        uart_putc(c == '\r' ? '\n' : c);
    }
    if (irq)
        plic_complete(irq);
}

void start_kernel() {
    uart_puts("\nStarting kernel ...\n");
    plic_init();
    uart_init();
    irq_enable();
    while (1)
        ;
}

// 4
// 在 sie CSR 中啟用 external interrupt delivery（SEIE）。
// 必須同時開 sie.SEIE 與 sstatus.SIE 才能讓外部中斷送到 S-mode。
void enable_external_interrupt() {
    // 開啟 supervisor external interrupt (SEIE)。
    // 把 sie 這個 CSR 裡的第 9 bit 設成 1，也就是 SEIE。
    // 也就是像 UART 這種透過 PLIC 送來的中斷，CPU 才會真的接收到
    // 讓 外部中斷 可以進到 S-mode
    asm volatile(
        "li t0, (1 << 9);"
        "csrs sie, t0;");
}