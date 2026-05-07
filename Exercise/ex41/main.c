extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);
extern int hextoi(const char* s, int n);
extern int align(int n, int byte);
extern int memcmp(const void* s1, const void* s2, int n);
extern void* alloc_page();

// TODO: Check the RAM disk base address
#define INITRD_BASE 0x0000000088200000
#define STACK_SIZE  0x1000

struct cpio_t {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
};

// 定義寄存器結構，對應 start.S 的儲存順序
struct pt_regs {
    unsigned long ra, sp, gp, tp, t0, t1, t2, s0, s1, a0, a1, a2, a3, a4, a5, a6, a7, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, t3, t4, t5, t6;
    unsigned long sepc, sstatus, scause, stval;
};

// 在 Initramfs 尋找 prog.bin, 並且在 U-Mode 執行它
int exec(const char* filename) {
    char* p = (char*)INITRD_BASE;
    while (memcmp(p + sizeof(struct cpio_t), "TRAILER!!!", 10)) {
        struct cpio_t* hdr = (struct cpio_t*)p;
        int namesize = hextoi(hdr->namesize, 8);
        int filesize = hextoi(hdr->filesize, 8);
        int headsize = align(sizeof(struct cpio_t) + namesize, 4);
        int datasize = align(filesize, 4);
        
        if (!memcmp(p + sizeof(struct cpio_t), filename, namesize)) {
            unsigned long target_address = (unsigned long)(p + headsize);
            // 配一塊 user stack
            void* user_stack = alloc_page();
            if (!user_stack) return -1;

            unsigned long user_sp = (unsigned long)user_stack + STACK_SIZE;
            unsigned long sstatus;

            // 設定 sstatus: SPP=0 (返回 U-mode), SPIE=1 (開啟中斷)
            asm volatile("csrr %0, sstatus" : "=r"(sstatus));
            sstatus &= ~(1L << 8); // 清除 SPP 位元 (Bit 8)
            sstatus |= (1L << 5);  // 設定 SPIE 位元 (Bit 5)

            // 準備跳轉至 User mode
            asm volatile(
                "csrw sepc, %0\n"
                "csrw sstatus, %1\n"
                "csrw sscratch, sp\n" // 存入現在 S-mode 的 kernel stack 位址, 下次切換回來 才知道在哪
                "mv sp, %2\n"
                "sret\n"
                : : "r"(target_address), "r"(sstatus), "r"(user_sp)
            );
        }
        p += headsize + datasize;
    }
    return -1;
}

// 被 start.S 呼叫
void do_trap(struct pt_regs* regs) {
    // (1) 打印 sepc 與 scause
    uart_puts("Trap! sepc: ");
    uart_hex(regs->sepc);
    uart_puts(", scause: ");
    uart_hex(regs->scause);
    uart_puts("\n");

    // (2) ecall 指令長度為 4，將 sepc 加 4 以跳過該指令
    regs->sepc += 4;
}

void start_kernel() {
    uart_puts("\nStarting kernel ...\n");
    if (exec("prog.bin"))
        uart_puts("Failed to exec user program!\n");
    while (1) {
        uart_putc(uart_getc());
    }
}
