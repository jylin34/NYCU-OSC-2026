#include <stdint.h>

/*
 * UART MMIO layout (16550-like) and helper macros.
 * 註：UART 是透過 MMIO 存取的週邊，必須以指標讀寫對應位址。
 * - RBR/THR: 接收/傳送暫存器
 * - LSR: Line Status Register（bit0 RX ready, bit5 THR empty/ready）
 * - IER: Interrupt Enable Register（bit0 RX, bit1 TX）
 */
unsigned long UART_BASE = 0xD4017000UL; // Default for OrangePi RV2
unsigned long UART_LSR_OFFSET = 0x14;  // Default for OrangePi RV2

#define UART_RBR  ((volatile unsigned char*)(UART_BASE + 0x0))
#define UART_THR  ((volatile unsigned char*)(UART_BASE + 0x0))
#define UART_LSR  ((volatile unsigned char*)(UART_BASE + UART_LSR_OFFSET)) 
#define LSR_DR    (1 << 0)
#define LSR_TDRQ  (1 << 5)
/* Interrupt Enable Register (16550 compatible) */
// SOC manual 16.3.4.5 Interrupt Enable Register
// qemu: 0x1 / orangepi: 0x4
#define UART_IER  ((volatile unsigned int*)(UART_BASE + 0x1))

#define RING_BUF_SIZE 256

struct ring_buffer {
    volatile unsigned char buffer[RING_BUF_SIZE];
    unsigned int head; /* read index */
    unsigned int tail; /* write index */
};

static struct ring_buffer rx_rb;
static struct ring_buffer tx_rb;

extern unsigned long boot_cpu_hartid;
extern unsigned long PLIC_BASE;  /* Discovered from FDT in main.c */
// UART 這個裝置 在 PLIC 裡面對應的 interrupt 編號
// OrangePi: sys_int_ap[42] -> UART0_int (K1 SoC User Manual p.117)
// QEMU: 10
extern unsigned long UART_PLIC_IRQ;  /* Discovered from FDT in main.c */

/* forward declarations */
int is_buf_full(struct ring_buffer *rb);
int is_buf_empty(struct ring_buffer *rb);
int buf_push(struct ring_buffer *rb, char c);
int buf_pop(struct ring_buffer *rb, char *out);
int uart_rx_buffer_push(char c);
int uart_rx_buffer_pop(char *c);
int uart_tx_buffer_push(char c);
int uart_tx_buffer_pop(char *c);
int uart_tx_buffer_empty(void);

/*
 * PLIC (Platform-Level Interrupt Controller) MMIO layout helpers.
 * PLIC 是外部中斷控制器，需透過固定的記憶體位址讀寫以設定優先度、enable 與 claim/complete。
 * PLIC_BASE 由 FDT 在 main.c 中動態發現。
 */
#define PLIC_PRIORITY(irq)   (PLIC_BASE + (irq) * 4)
#define PLIC_ENABLE(hart)    (PLIC_BASE + 0x002080 + (hart) * 0x0100)
#define PLIC_THRESHOLD(hart) (PLIC_BASE + 0x201000 + (hart) * 0x2000)
#define PLIC_CLAIM(hart)     (PLIC_BASE + 0x201004 + (hart) * 0x2000)


/* Initialize PLIC for UART on the current hart: set priority, enable, threshold=0 */
void uart_plic_init_for_hart(void) {
    unsigned int hartid = (unsigned int)boot_cpu_hartid;
    unsigned int irq = UART_PLIC_IRQ;

    // 把 UART 這個 interrupt 的 priority 設成 1
    volatile unsigned int *pprio = (volatile unsigned int *)PLIC_PRIORITY(irq);
    // 這是 PLIC hardware interrupt 的 priority, 跟助教給的測資的 P4 P3 P2 P1 的 add_task priority 不同
    // hardware interrupt 跟 software interrupt 的 priority 不會做比較
    *pprio = 1;

    volatile unsigned int *pen_base = (volatile unsigned int *)PLIC_ENABLE(hartid);
    // 由於一個 32 位元的暫存器只能控制 32 個中斷，我們必須知道目標 irq 落在第幾個「字組（Word）」裡。
    unsigned int word = irq / 32; 
    volatile unsigned int *pen_word = pen_base + word;
    *pen_word |= (1u << (irq % 32));

    // 把 threshold 設定 0
    // priority > threshold 的時後，才會把 interrupt. 傳給 CPU
    volatile unsigned int *pth = (volatile unsigned int *)PLIC_THRESHOLD(hartid);
    *pth = 0;
}

/* Simple UART ISR: handle all work directly, no task queue */
void uart_plic_handle_interrupt(void) {
    unsigned int hartid = (unsigned int)boot_cpu_hartid;
    // 讀取 PLIC_CLAIM 會拿到目前優先權最高的中斷編號。在 K1 SoC 上，如果是 UART0 觸發，這裡會拿到 42。
    volatile unsigned int *claim = (volatile unsigned int *)PLIC_CLAIM(hartid);
    unsigned int irq = *claim;
    if (irq == 0) return;

    if (irq == UART_PLIC_IRQ) {
        /* RX path: read all available bytes into buffer */
        while ((*UART_LSR & LSR_DR) != 0) { // 當 rx 裡面有東西
            char c = (char)(*UART_RBR);
            uart_rx_buffer_push(c); // 把它拿出來 放到 rx ring buffer
        }

        /* TX path: send pending bytes if UART is ready */
        char tx_c;
        while ((*UART_LSR & LSR_TDRQ) != 0) { // 當 tx 是空的 
            if (uart_tx_buffer_pop(&tx_c) == 0) { // 從 tx ring buffer 那東西放到 tx
                *UART_THR = (unsigned char)tx_c;
            } else {
                break;
            }
        }

        /* Re-enable UART interrupts */
        unsigned char ier = 0;
        ier |= (1 << 0);  /* RX interrupt always on */
        if (!uart_tx_buffer_empty()) {
            ier |= (1 << 1);  /* TX interrupt only if pending data */
        }
        *UART_IER = ier;
    }

    /* Complete PLIC claim */
    *claim = irq;
}

int is_buf_full(struct ring_buffer *rb) {
    return ((rb->tail + 1) % RING_BUF_SIZE) == rb->head;
}

int is_buf_empty(struct ring_buffer *rb) {
    return rb->head == rb->tail;
}

int buf_push(struct ring_buffer *rb, char c) {
    if (is_buf_full(rb))
        return -1;
    rb->buffer[rb->tail] = (unsigned char)c;
    rb->tail = (rb->tail + 1) % RING_BUF_SIZE;
    return 0;
}

int buf_pop(struct ring_buffer *rb, char *out) {
    if (is_buf_empty(rb))
        return -1;
    *out = (char)rb->buffer[rb->head];
    rb->head = (rb->head + 1) % RING_BUF_SIZE;
    return 0;
}

void uart_buffer_init(void) {
    rx_rb.head = 0;
    rx_rb.tail = 0;
    tx_rb.head = 0;
    tx_rb.tail = 0;
}

int uart_rx_buffer_push(char c) {
    if (c == '\r')
        c = '\n';
    return buf_push(&rx_rb, c);
}

int uart_rx_buffer_pop(char *c) {
    if (buf_pop(&rx_rb, c) < 0)
        return -1;
    return 0;
}

int uart_tx_buffer_push(char c) {
    return buf_push(&tx_rb, c);
}

int uart_tx_buffer_pop(char *c) {
    if (buf_pop(&tx_rb, c) < 0)
        return -1;
    return 0;
}

int uart_rx_buffer_empty(void) {
    return is_buf_empty(&rx_rb);
}

int uart_tx_buffer_empty(void) {
    return is_buf_empty(&tx_rb);
}

int uart_rx_buffer_full(void) {
    return is_buf_full(&rx_rb);
}

int uart_tx_buffer_full(void) {
    return is_buf_full(&tx_rb);
}

void uart_drain_tx_polling(void) {
    char c;
    while (((*UART_LSR & LSR_TDRQ) != 0) && buf_pop(&tx_rb, &c) == 0)
        *UART_THR = (unsigned char)c;
}

void uart_fill_rx_polling(void) {
    while ((*UART_LSR & LSR_DR) != 0) {
        char c = (char)(*UART_RBR);
        if (c == '\r')
            c = '\n';
        if (buf_push(&rx_rb, c) < 0)
            break;
    }
}

/* Enable/disable UART interrupts: bit0 = RX, bit1 = TX (ETBEI) */
// soc manual p.752
void uart_enable_irqs(int rx_enable, int tx_enable) {
    unsigned char v = 0;
    if (rx_enable)
        v |= (1 << 0);
    if (tx_enable)
        v |= (1 << 1);
    *UART_IER = v;
}

/* Enable supervisor external interrupt delivery (SEIE in sie CSR). */
// s-mode 開啟 external interrupt enable
void uart_enable_external_interrupt(void) {
    asm volatile("csrs sie, %0" :: "r"(1UL << 9));
}

char uart_getc() {
    char c;
    while (uart_rx_buffer_pop(&c) < 0); // waiting for buffer
    return c;
}

void uart_putc(char c) {
    if (c == '\n') {
        while (uart_tx_buffer_push('\r') < 0)
            ;
    }

    while (uart_tx_buffer_push(c) < 0)
        ;

    /* keep RX enabled and turn on TX interrupt so ISR drains tx buffer */
    uart_enable_irqs(1, 1);

    /* kick once if THR is already ready; ISR will finish the rest */
    // TDRQ: 代表 UART 的 Transmit Holding Register（UART_THR）目前是空的
    if ((*UART_LSR & LSR_TDRQ) != 0) { // 如果 UART_THR 是空的
        char first;
        if (uart_tx_buffer_pop(&first) == 0) // 並且 tx ring buffer 現在裡面有字
            *UART_THR = (unsigned char)first; // 把字丟進 UART_THR
    }
}

void uart_puts(const char* s) {
    while (*s)
        uart_putc(*s++);
}

void uart_hex(unsigned long h) {
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc(n);
    }
}
