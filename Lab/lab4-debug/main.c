#include <stdint.h>
#include <stddef.h>

// Forward declarations for FDT functions implemented in fdt.c
extern int fdt_path_offset(const void *fdt, const char *path);
extern const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name, int *lenp);
extern uint32_t bswap32(uint32_t x);
extern uint64_t bswap64(uint64_t x);
extern void *fdt_ptr;

// Forward declarations for CPIO functions implemented in cpio.c
extern void initrd_list(const void *rd);
extern void initrd_cat(const void *rd, const char *filename);

extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);

extern unsigned long UART_BASE;
extern unsigned long UART_LSR_OFFSET;

void *cpio_base = 0;
void *cpio_end = 0;
unsigned long mem_base = 0;
unsigned long mem_size = 0;
unsigned long fdt_size = 0;

struct reserved_region {
    unsigned long base;
    unsigned long size;
    char name[32];
};

#define MAX_RESERVED_REGIONS 16
struct reserved_region fdt_reserved_regions[MAX_RESERVED_REGIONS];
int fdt_reserved_regions_count = 0;

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

#define SBI_EXT_SET_TIMER 0x0
#define SBI_EXT_SHUTDOWN  0x8
#define SBI_EXT_BASE      0x10

enum sbi_ext_base_fid {
    SBI_EXT_BASE_GET_SPEC_VERSION,
    SBI_EXT_BASE_GET_IMP_ID,
    SBI_EXT_BASE_GET_IMP_VERSION,
    SBI_EXT_BASE_PROBE_EXT,
    SBI_EXT_BASE_GET_MVENDORID,
    SBI_EXT_BASE_GET_MARCHID,
    SBI_EXT_BASE_GET_MIMPID,
};

struct sbiret {
    long error;
    long value;
};

struct sbiret sbi_ecall(int ext,
                        int fid,
                        unsigned long arg0,
                        unsigned long arg1,
                        unsigned long arg2,
                        unsigned long arg3,
                        unsigned long arg4,
                        unsigned long arg5) {
    struct sbiret ret;
    register unsigned long a0 asm("a0") = (unsigned long)arg0;
    register unsigned long a1 asm("a1") = (unsigned long)arg1;
    register unsigned long a2 asm("a2") = (unsigned long)arg2;
    register unsigned long a3 asm("a3") = (unsigned long)arg3;
    register unsigned long a4 asm("a4") = (unsigned long)arg4;
    register unsigned long a5 asm("a5") = (unsigned long)arg5;
    register unsigned long a6 asm("a6") = (unsigned long)fid;
    register unsigned long a7 asm("a7") = (unsigned long)ext;
    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    ret.error = a0;
    ret.value = a1;
    return ret;
}

long sbi_get_spec_version(void) {
    struct sbiret ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0);
    return ret.value;
}

long sbi_get_impl_id(void) {
    struct sbiret ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_ID, 0, 0, 0, 0, 0, 0);
    return ret.value;
}

long sbi_get_impl_version(void) {
    struct sbiret ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_VERSION, 0, 0, 0, 0, 0, 0);
    return ret.value;
}

long sbi_probe_extension(int extid) {
    struct sbiret ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_PROBE_EXT, extid, 0, 0, 0, 0, 0);
    return ret.value;
}


static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

static inline const void* align_up(const void* ptr, size_t align) {
    return (const void*)(((uintptr_t)ptr + align - 1) & ~(align - 1));
}

void devicetree_early_init(void *fdt) {
    if (!fdt) return;

    struct fdt_header {
        uint32_t magic;
        uint32_t totalsize;
        uint32_t off_dt_struct;
        uint32_t off_dt_strings;
        uint32_t off_mem_rsvmap;
    };

    // --- 0. 解析 FDT Header ---
    struct fdt_header *h = (struct fdt_header *)fdt;
    fdt_size = bswap32(h->totalsize);

    // --- 0-1. 解析 Memory Reservation Block (mem_rsvmap) (Lab3) ---
    // https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html#memory-reservation-block
    // The memory reservation block provides the client program with a list of areas in physical memory which are reserved; 
    // that is, which shall not be used for general memory allocations.
    const uint64_t *rsv = (const uint64_t *)((const char *)fdt + bswap32(h->off_mem_rsvmap));
    while (1) {
        /*
         * struct fdt_reserve_entry {
         *  uint64_t address;
         *  uint64_t size;
         * };
        */ 
        uint64_t base = bswap64(rsv[0]);
        uint64_t size = bswap64(rsv[1]);
        if (base == 0 && size == 0) break;
        // 把 memory reservation block 裡面的所有區塊 填寫進 fdt_reserved_regions array
        if (fdt_reserved_regions_count < MAX_RESERVED_REGIONS) {
            /*
             * struct reserved_region {
             *  unsigned long base;
             *  unsigned long size;
             *  char name[32];
             * };
            */ 
            fdt_reserved_regions[fdt_reserved_regions_count].base = base;
            fdt_reserved_regions[fdt_reserved_regions_count].size = size;
            strncpy(fdt_reserved_regions[fdt_reserved_regions_count].name, "mem_rsvmap", 31);
            fdt_reserved_regions_count++;
        }
        rsv += 2; // 因為每一個 struct 裡面是一個 address 跟一個 size
    }

    int offset;
    int len;
    const void *prop;

    // --- 1. UART 初始化 (Lab 2) ---
    offset = fdt_path_offset(fdt, "/soc/serial");
    if (offset >= 0) {
        prop = fdt_getprop(fdt, offset, "reg", &len);
        if (prop) {
            const uint64_t *reg = (const uint64_t *)prop;
            UART_BASE = bswap64(reg[0]);
            UART_LSR_OFFSET = 0x14;
        }
    } else {
        offset = fdt_path_offset(fdt, "/soc/uart");
        if (offset >= 0) {
            prop = fdt_getprop(fdt, offset, "reg", &len);
            if (prop) {
                const uint64_t *reg = (const uint64_t *)prop;
                UART_BASE = bswap64(reg[0]);
                UART_LSR_OFFSET = 0x5;
            }
        }
    }

    // --- 2. 解析記憶體範圍 (/memory) (Lab3) ---
    // https://devicetree-specification.readthedocs.io/en/stable/devicenodes.html#memory-node
    // A memory device node is required for all devicetrees and describes the physical memory layout for the system. 
    // If a system has multiple ranges of memory, multiple memory nodes can be created, 
    // or the ranges can be specified in the reg property of a single memory node.
    offset = fdt_path_offset(fdt, "/memory");
    if (offset >= 0) {
        // /memory "reg" node property 
        // Consists of an arbitrary number of address and size pairs that specify the physical address and size of the memory ranges.
        prop = fdt_getprop(fdt, offset, "reg", &len);
        if (prop && len >= 16) {
            const uint64_t *reg = (const uint64_t *)prop;
            // reg[0] = address 1 / reg[1] = size 1
            // reg[2] = address 2 / reg[3] = size 2
            mem_base = bswap64(reg[0]);
            mem_size = bswap64(reg[1]);
            // QEMU: Memory Base: 0x0000000080000000, Size: 0x0000000010000000 (256MB)
            // OrangePi: Memory Base: 0x0000000000000000 Size: 0x0000_0000_7FFF_FFFF (2GB)
        }
    }   

    // --- 3. 解析 initramfs 範圍 (/chosen) (Lab2) ---
    offset = fdt_path_offset(fdt, "/chosen");
    if (offset >= 0) {
        prop = fdt_getprop(fdt, offset, "linux,initrd-start", &len);
        if (prop) {
            if (len == 4) {
                cpio_base = (void *)(uint64_t)bswap32(*(const uint32_t *)prop);
            } else if (len == 8) {
                uint64_t start;
                const char *p = (const char *)prop;
                uint32_t high = bswap32(*(const uint32_t *)p);
                uint32_t low = bswap32(*(const uint32_t *)(p + 4));
                start = ((uint64_t)high << 32) | low;
                cpio_base = (void *)start;
            }
        }

        prop = fdt_getprop(fdt, offset, "linux,initrd-end", &len);
        if (prop) {
            if (len == 4) {
                cpio_end = (void *)(uint64_t)bswap32(*(const uint32_t *)prop);
            } else if (len == 8) {
                uint64_t end;
                const char *p = (const char *)prop;
                uint32_t high = bswap32(*(const uint32_t *)p);
                uint32_t low = bswap32(*(const uint32_t *)(p + 4));
                end = ((uint64_t)high << 32) | low;
                cpio_end = (void *)end;
            }
        }
    }

    // --- 4. 解析 /reserved-memory 節點 (Lab3) ---
    offset = fdt_path_offset(fdt, "/reserved-memory");
    if (offset >= 0) {
        const uint32_t *p = (const uint32_t *)((const char *)fdt + offset);
        p++; 
        p = (const uint32_t *)align_up((const char *)p + strlen((const char *)p) + 1, 4);

        int depth = 1;
        while (depth > 0) {
            const uint32_t *node_ptr = p;
            uint32_t token = bswap32(*p++);
            if (token == FDT_BEGIN_NODE) {
                const char *node_name = (const char *)p;
                if (depth == 1) {
                    int child_off = (const char *)node_ptr - (const char *)fdt;
                    int reg_len;
                    const void *reg_prop = fdt_getprop(fdt, child_off, "reg", &reg_len);
                    if (reg_prop && reg_len >= 16) {
                        const uint64_t *reg = (const uint64_t *)reg_prop;
                        if (fdt_reserved_regions_count < MAX_RESERVED_REGIONS) {
                            fdt_reserved_regions[fdt_reserved_regions_count].base = bswap64(reg[0]);
                            fdt_reserved_regions[fdt_reserved_regions_count].size = bswap64(reg[1]);
                            strncpy(fdt_reserved_regions[fdt_reserved_regions_count].name, node_name, 31);
                            fdt_reserved_regions_count++;
                        }
                    }
                }
                p = (const uint32_t *)align_up(node_name + strlen(node_name) + 1, 4);
                depth++;
            } else if (token == FDT_END_NODE) {
                depth--;
            } else if (token == FDT_PROP) {
                uint32_t prop_len = bswap32(*p++);
                p++; 
                p = (const uint32_t *)align_up((const char *)p + prop_len, 4);
            } else if (token == FDT_NOP) {
                continue;
            } else if (token == FDT_END) {
                break;
            }
        }
    }
}

extern void mm_init(unsigned long base, unsigned long size);
extern void memory_reserve(unsigned long base, unsigned long size);
extern void buddy_test();
extern void test_alloc_1();

// =============================== Lab 4 debug Start =====================================

extern struct page* alloc_pages(int order);
extern struct page* mem_map;
extern unsigned long buddy_memory_base;

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

struct page {
    struct list_head list;
    int order;
    int refcount;
    int is_slab;
    uint32_t slab_id;
};

struct pt_regs {
    unsigned long ra;
    unsigned long sp;
    unsigned long gp;
    unsigned long tp;
    unsigned long t0;
    unsigned long t1;
    unsigned long t2;
    unsigned long s0;
    unsigned long s1;
    unsigned long a0;
    unsigned long a1;
    unsigned long a2;
    unsigned long a3;
    unsigned long a4;
    unsigned long a5;
    unsigned long a6;
    unsigned long a7;
    unsigned long s2;
    unsigned long s3;
    unsigned long s4;
    unsigned long s5;
    unsigned long s6;
    unsigned long s7;
    unsigned long s8;
    unsigned long s9;
    unsigned long s10;
    unsigned long s11;
    unsigned long t3;
    unsigned long t4;
    unsigned long t5;
    unsigned long t6;
    unsigned long sepc;
    unsigned long sstatus;
    unsigned long scause;
    unsigned long stval;
};

#define STACK_SIZE  0x1000

/**
 * @brief Convert a hexadecimal string to integer
 */
static int hextoi(const char* s, int n) { // cpio: n hex digits
    int r = 0;
    while (n-- > 0) {
        r = r << 4;
        if (*s >= 'A')
            r += *s++ - 'A' + 10;
        else if (*s >= '0')
            r += *s++ - '0';
        else
            s++;
    }
    return r;
}

/**
 * @brief Align a number to the nearest multiple of a given number
 */
static int align(int n, int byte) {
    return (n + byte - 1) & ~(byte - 1);
}

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

int exec(const char *filename) {
    if (!cpio_base) {
        uart_puts("Error: initramfs not found in device tree.\n");
        return -1;
    }

    const char *ptr = (const char *)cpio_base;
    while (1) {
        const struct cpio_t *header = (const struct cpio_t *)ptr;

        if (strncmp(header->magic, "070701", 6) != 0) {
            uart_puts("Error: Invalid initramfs format.\n");
            return -1;
        }

        uint32_t namesize = (uint32_t)hextoi(header->namesize, 8);
        uint32_t filesize = (uint32_t)hextoi(header->filesize, 8);
        const char *cur_filename = ptr + sizeof(struct cpio_t);

        if (!strcmp(cur_filename, "TRAILER!!!")) {
            break;
        }

        uint32_t data_offset = (uint32_t)align(sizeof(struct cpio_t) + namesize, 4);
        if (!strcmp(cur_filename, filename)) {
            unsigned long target_address = (unsigned long)(ptr + data_offset);
            struct page *user_stack_page = alloc_pages(0); // 分配 user stack, 一個 (2^0) page = 4KB
            if (!user_stack_page) {
                uart_puts("Error: failed to allocate user stack.\n");
                return -1;
            }

            // 在 mem_map 裡面的第幾個 page * page 大小 (4096 Bytes)
            unsigned long user_stack = buddy_memory_base + (unsigned long)(user_stack_page - mem_map) * 0x1000UL; 
            unsigned long user_sp = user_stack + STACK_SIZE;
            unsigned long sstatus;

            // 從 CSR sstatus 讀出目前的值，存到 C 變數 sstatus。
            asm volatile("csrr %0, sstatus" : "=r"(sstatus));
            // 把 sstatus 第 8 bit 清成 0。 (bitwise NOT) 
            // SPP = 0 代表 sret 後要回到 U-mode。
            sstatus &= ~(1UL << 8);
            // 把 sstatus 第 5 bit 設成 1。
            // SPIE ?
            sstatus |= (1UL << 5);

            asm volatile(
                "csrw sepc, %0\n"
                "csrw sstatus, %1\n"
                "csrw sscratch, sp\n" // 把目前 S-mode 的 stack pointer 存到 sscratch
                "mv sp, %2\n"
                "sret\n" // 跳進 U-mode (riscv 的 assembly 語法)
                :
                : "r"(target_address), "r"(sstatus), "r"(user_sp) // %0 %1 %2 
                : "memory");

            return 0;
        }

        ptr += align(data_offset + filesize, 4);
    }

    uart_puts("Error: program not found in initramfs.\n");
    return -1;
}

// wrapper function to call sbi_ecall to trigger timer interrupt
static inline void sbi_set_timer(unsigned long stime_value) { // 絕對值
    /* SBI_EXT_SET_TIMER: 0 (set_timer), fid 0 (function id 0) */
    sbi_ecall(SBI_EXT_SET_TIMER, 0, stime_value, 0, 0, 0, 0, 0);
    // S-mode 的作業系統）透過 SBI（ecall）叫韌體（例如 OpenSBI）幫我把下一次
    // 定時器事件設定到 stime_value，也就是請韌體把 CLINT 的 mtimecmp 設成 stime_value。
}

// static 只有當前檔案可以用的 function
// inline 告訴編譯器「可把呼叫處展開成函式本體以省掉呼叫開銷（內聯）」。
static inline unsigned long rdtime(void) {
    unsigned long t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

static unsigned long ticks_per_sec = 24000000UL; // 硬編碼為 24000000 (DTB timebase-frequency) / qemu: 10000000UL
static unsigned long boot_time = 0;

static void timer_init(void) {
    unsigned long now = rdtime();
    unsigned long target = now + 2UL * ticks_per_sec; // 2 秒後
    boot_time = now;

    sbi_set_timer(target);

    // 開 timer interrupt source: STIE (sie bit 5)
    asm volatile("csrs sie, %0" :: "r"(1UL << 5));
    // 開全域中斷: SIE (sstatus bit 1)
    asm volatile("csrsi sstatus, (1 << 1)");
}

void do_trap(struct pt_regs *regs) {

    unsigned long scause = regs->scause;
    unsigned long is_interrupt = scause >> ((sizeof(unsigned long) * 8) - 1); // 8 * 8 - 1 = 63 為了兼容 rv32 rv64
    // 取 scause 的 MSB, 代表 interrupt bit, 是 1 的話 代表是 interrupt, 是 0 的話代表是 exception
    unsigned long cause_code = scause & 0xffUL; // 取 scause 的最低的 8 位
    // 如果 cause_code = 5 就代表是 timer interrupt

    if (is_interrupt && cause_code == 5UL) {// exercise 2: supervisor timer interrupt
        unsigned long now = rdtime();
        unsigned long seconds = 0;
        char msg[32];
        int pos = 0;

        if (now >= boot_time) {
            seconds = (now - boot_time) / ticks_per_sec;
        }

        while (pos < (int)(sizeof(msg) - 1) && "boot time: "[pos] != '\0') {
            msg[pos] = "boot time: "[pos];
            pos++;
        }

        if (seconds == 0) {
            msg[pos++] = '0';
        } else {
            char digits[32];
            int digit_count = 0;

            while (seconds > 0 && digit_count < (int)sizeof(digits)) {
                digits[digit_count++] = '0' + (seconds % 10);
                seconds /= 10;
            }

            while (digit_count-- > 0 && pos < (int)(sizeof(msg) - 1)) {
                msg[pos++] = digits[digit_count];
            }
        }

        if (pos < (int)(sizeof(msg) - 1)) {
            msg[pos++] = '\n';
        }
        msg[pos] = '\0';

        uart_puts(msg);

        sbi_set_timer(now + 2UL * ticks_per_sec);
        return;
    }

    uart_puts("=== S-Mode trap ===\n");
    uart_puts("scause: ");
    uart_hex(regs->scause);
    uart_puts("\n");
    uart_puts("sepc: ");
    uart_hex(regs->sepc);
    uart_puts("\n");
    uart_puts("stval: ");
    uart_hex(regs->stval);
    uart_puts("\n");

    if (regs->scause == 8UL) {
        regs->sepc += 4; // 這段是在處理「U-mode 的 ecall」後，避免無限重複 trap。
    }
}

// =============================== Lab 4 debug End =====================================

extern char _start[];
extern char _image_end[];
extern char _end[];

void start_kernel(unsigned long hartid);

// Lab2: advance - Bootloader Self-Relocation
// 0x00200000 -> 0x20000000
// Lab2: advance - Bootloader Self-Relocation
void relocate_bootloader(unsigned long hartid) {
    unsigned long target_addr = 0x20000000;

    // 1. 取得當前執行的實體位址 (PC)
    unsigned long current_pc;
    asm volatile("auipc %0, 0" : "=r"(current_pc));

    // 2. 判斷是否已經在「新家」
    if (current_pc >= target_addr) {
        return; 
    }

    // 3. 開始搬家
    unsigned long size = (unsigned long)_image_end - (unsigned long)_start;
    unsigned long *src = (unsigned long *)_start;
    unsigned long *dst = (unsigned long *)target_addr;
    
    for (unsigned long i = 0; i < size / 8; i++) {
        dst[i] = src[i];
    }

    // 4. 計算偏移量與新地址
    unsigned long offset = target_addr - (unsigned long)_start;
    unsigned long new_start_kernel = (unsigned long)&start_kernel + offset;

    // 🚀 關鍵修復：取得目前的 sp 和 gp，並加上位移！
    unsigned long current_sp, current_gp;
    asm volatile("mv %0, sp" : "=r"(current_sp));
    asm volatile("mv %0, gp" : "=r"(current_gp));

    unsigned long new_sp = current_sp + offset;
    unsigned long new_gp = current_gp + offset; 

    // 5. 信仰之躍！切換 SP 與 GP，跳轉到新家
    asm volatile(
        "mv a0, %0\n"       
        "mv sp, %1\n"       // 使用新家的 Stack
        "mv gp, %2\n"       // 👈 告訴 CPU 使用新家的 Global Pointer
        "jr %3\n"           // 跳轉
        :
        : "r"(hartid), "r"(new_sp), "r"(new_gp), "r"(new_start_kernel)
        : "a0", "memory"
    );

    while(1); 
}

void start_kernel(unsigned long hartid) {
    unsigned long boot_cpu_hartid = hartid;
    // relocate_bootloader(boot_cpu_hartid);
    timer_init();
    devicetree_early_init(fdt_ptr);
    
    // 啟動記憶體管理系統 (mm_init)
    // 原本寫 if(mem_base & mem_size) 這樣會有問題
    // 因為 orangepi 的 base 就是 00000000 C 會認為那是 false 就沒有去執行 mm_init()
    if (mem_size > 0) {
        uart_puts("\nInitializing Memory...\n");
        mm_init(mem_base, mem_size); 
    }

    uart_puts("\n--- Reserved Memory Regions (Lab 3: Advance Exercise 2 - Reserved Memory) ---\n");
    uart_puts("Hardware Reserved:  0x00000000 - 0x00001000\n");
    uart_puts("Kernel Image (Without mem_map): ");
    uart_hex((unsigned long)_start);
    uart_puts(" - ");
    uart_hex((unsigned long)_end);
    uart_puts("\n");
    uart_puts("Device Tree (DTB):  ");
    uart_hex((unsigned long)fdt_ptr);
    uart_puts(" - ");
    uart_hex((unsigned long)fdt_ptr + fdt_size);
    uart_puts(" (size: ");
    uart_hex(fdt_size);
    uart_puts(")\n");

    if (cpio_base) {
        uart_puts("Initramfs:          ");
        uart_hex((unsigned long)cpio_base);
        uart_puts(" - ");
        uart_hex((unsigned long)cpio_end);
        uart_puts("\n");
    }

    for (int i = 0; i < fdt_reserved_regions_count; i++) {
        uart_puts("FDT Reserved:       ");
        uart_hex(fdt_reserved_regions[i].base);
        uart_puts(" - ");
        uart_hex(fdt_reserved_regions[i].base + fdt_reserved_regions[i].size);
        uart_puts(" (");
        uart_puts(fdt_reserved_regions[i].name);
        uart_puts(")\n");
    }
    uart_puts("-------------------------------\n");

    uart_puts("\nStarting kernel ...\n");
    uart_puts("UART_BASE initialized from DTB: ");
    uart_hex(UART_BASE);
    uart_puts("\n");

    uart_puts("Memory Base: ");
    uart_hex(mem_base);
    uart_puts(", Size: ");
    uart_hex(mem_size);
    uart_puts("\n");

    uart_puts("CPIO_BASE initialized from DTB: ");
    if (cpio_base) {
        uart_hex((unsigned long)cpio_base);
    } else {
        uart_puts("None");
    }
    uart_puts("\n");

    char buf[256];
    int idx = 0;

    while (1) {
        uart_puts("opi-rv2> ");
        idx = 0;
        while (1) {
            char c = uart_getc();

            if (c == '\n') {
                uart_putc('\n');
                buf[idx] = '\0';
                break;
            } else if (c == 127 || c == '\b') {
                if (idx > 0) {
                    idx--;
                    uart_puts("\b \b");
                }
            } else {
                if (idx < 255) {
                    buf[idx++] = c;
                    uart_putc(c);
                }
            }
        }

        if (idx == 0) continue;

        if (strcmp(buf, "help") == 0) {
            uart_puts("help      : print this help menu\n");
            uart_puts("hello     : print Hello World!\n");
            uart_puts("info      : print system information\n");
            uart_puts("load      : load kernel image from host computer\n");
            uart_puts("ls        : list files in initramfs\n");
            uart_puts("cat       : display file content in initramfs\n");
            uart_puts("buddy     : test the buddy system memory allocator\n");
            uart_puts("test_lab3 : test the memory allocator (test_alloc_1)\n");
            uart_puts("05/08 21:41\n");
        } 
        else if (strcmp(buf, "buddy") == 0) {
            buddy_test();
        }
        else if (strcmp(buf, "test_lab3") == 0) {
            test_alloc_1();
        }
        else if (strcmp(buf, "hello") == 0) {
            uart_puts("Hello World!\n");
        } 
        else if (strcmp(buf, "info") == 0) {
            uart_puts("SBI specification version: ");
            uart_hex(sbi_get_spec_version());
            uart_puts("\n");

            uart_puts("SBI implementation ID:    ");
            uart_hex(sbi_get_impl_id());
            uart_puts("\n");

            uart_puts("SBI implementation version: ");
            uart_hex(sbi_get_impl_version());
            uart_puts("\n");
        } 
        else if (strcmp(buf, "load") == 0) {
            uart_puts("Please use uploader.py to send kernel image via UART.\n");   
           
            void *safe_fdt_ptr = fdt_ptr;

            uint32_t magic = 0; 
            uint32_t size = 0; 
            uint8_t * load_ptr = (uint8_t *)0x20000000; 
            
            for (uint8_t i = 0; i < 4; i ++) { 
                ((char *)&magic)[i] = uart_getc();
            }

            if (magic != 0x544F4F42) {
                uart_puts("Error: Invalid Magic Number, stop loading.\n");
            } 
            else {
                uart_puts("Magic Number Correct! Receiving size...\n");
                for (uint8_t i = 0; i < 4; i ++) {
                    ((char *)&size)[i] = uart_getc();
                }
                uart_puts("Get the size of the file.\n");
                for (uint32_t i = 0; i < size; i ++) {
                    load_ptr[i] = uart_getc();
                    if (i % 4096 == 0) {
                        uart_puts(".");
                    }
                }
            
                void (*kernel_entry)(unsigned long, void *) = (void (*)(unsigned long, void *))0x20000000;
                uart_puts("Set program counter to 0x20000000 and passing FDT pointer...\n");
                kernel_entry(0, safe_fdt_ptr); // a0 & a1 register 
            }
        }
        else if (strcmp(buf, "ls") == 0) {
            if (cpio_base) {
                initrd_list(cpio_base);
            } else {
                uart_puts("Error: initramfs not found in device tree.\n");
            }
        }
        else if (strncmp(buf, "cat ", 4) == 0) {
            if (cpio_base) {
                initrd_cat(cpio_base, buf + 4);
            } else {
                uart_puts("Error: initramfs not found in device tree.\n");
            }
        }
        else if (strncmp(buf, "exec ", 5) == 0) {
            const char *filename = buf + 5;

            while (*filename == ' ') {
                filename++;
            }

            if (*filename == '\0') {
                uart_puts("Error: missing program name.\n");
            } else {
                if (exec(filename) < 0) {
                    uart_puts("Failed to exec user program.\n");
                }
            }
        }
        else {
            uart_puts("Unknown command: ");
            uart_puts(buf);
            uart_puts("\n");
        }
    }
}
