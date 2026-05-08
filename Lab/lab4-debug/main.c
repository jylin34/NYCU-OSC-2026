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
        else {
            uart_puts("Unknown command: ");
            uart_puts(buf);
            uart_puts("\n");
        }
    }
}
