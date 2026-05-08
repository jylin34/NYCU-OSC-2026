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

void devicetree_early_init(void *fdt) {
    if (!fdt) return;

    int offset;
    int len;
    const void *prop;

    // --- 1. UART 初始化 (互斥偵測) ---
    // 先找板子的路徑
    offset = fdt_path_offset(fdt, "/soc/serial");
    if (offset >= 0) {
        prop = fdt_getprop(fdt, offset, "reg", &len);
        if (prop) {
            const uint64_t *reg = (const uint64_t *)prop;
            UART_BASE = bswap64(reg[0]);
            UART_LSR_OFFSET = 0x14;
        }
    } 
    // 如果板子路徑沒找到，再找 QEMU 的路徑
    else {
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

    // --- 2. 解析 initramfs 位址 (一定要執行) ---
    offset = fdt_path_offset(fdt, "/chosen");
    if (offset >= 0) {
        prop = fdt_getprop(fdt, offset, "linux,initrd-start", &len);
        // len: 這個 property 的長度
        if (prop) {
            if (len == 4) {
                cpio_base = (void *)(uint64_t)bswap32(*(const uint32_t *)prop);
            } else if (len == 8) {
                // device tree requires 4-bytes alignment
                uint64_t start;
                const char *p = (const char *)prop;
                uint32_t high = bswap32(*(const uint32_t *)p);
                uint32_t low = bswap32(*(const uint32_t *)(p + 4));
                start = ((uint64_t)high << 32) | low;
                cpio_base = (void *)start;
            }
        }
    }
}

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

    uart_puts("\nStarting kernel ...\n");
    uart_puts("UART_BASE initialized from DTB: ");
    uart_hex(UART_BASE);
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
            uart_puts("help    : print this help menu\n");
            uart_puts("hello   : print Hello World!\n");
            uart_puts("info    : print system information\n");
            uart_puts("load    : load kernel image from host computer\n");
            uart_puts("ls      : list files in initramfs\n");
            uart_puts("cat     : display file content in initramfs\n");
            uart_puts("This is the kernel responsible for loading new kernel without self-relocation!\n2026/05/08 22:16\n");
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
            // QEMU: 0x82000000
            // OrangePi: 0x20000000
            uint8_t * load_ptr = (uint8_t *)0x20000000; 
            
            for (uint8_t i = 0; i < 4; i ++) { 
                ((char *)&magic)[i] = uart_getc();
            }

            if (magic != 0x544F4F42) { // BOOT 的 Magic Number
                uart_puts("Error: Invalid Magic Number, stop loading.\n");
            } 
            else {
                uart_puts("Magic Number Correct! Receiving size...\n");
            
                // 這 4 個 bytes 代表接下來要傳輸的檔案總共有多少個 bytes
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
            
                // 定義 kernel_entry pointer 並且指向 0x20000000
                void (*kernel_entry)(unsigned long, void *) = (void (*)(unsigned long, void *))0x20000000;
                // 呼叫 kernel_entry CPU 會把 Program Counter 指向那個位址，並傳遞 FDT 指標 (a1)
                uart_puts("Set program counter to 0x20000000 and passing FDT pointer...\n");
                kernel_entry(0, safe_fdt_ptr);
                // 傳入的參數分別代表 a0(Hardware Thread ID) / a1(file device tree)
                // ，Hart ID 就是每個 CPU 核心專屬的「員工編號」，用來讓系統在開機時辨認現在是哪一個硬體工人在幹活。
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
