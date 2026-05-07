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
extern void uart_buffer_init(void);
extern void uart_enable_irqs(int rx_enable, int tx_enable);
extern void uart_enable_external_interrupt(void);
extern void uart_plic_init_for_hart(void);
extern void uart_plic_handle_interrupt(void);

extern unsigned long UART_BASE;
extern unsigned long UART_LSR_OFFSET;
unsigned long boot_cpu_hartid = 0;
unsigned long PLIC_BASE = 0xc000000UL; /* default for QEMU; will be overwritten by FDT */
// Qemu: 0xc000000 Orangepi: 0xE0000000 ?
unsigned long UART_PLIC_IRQ = 10UL; /* default for QEMU; will be overwritten by FDT */
// UART 這個裝置 在 PLIC 裡面對應的 interrupt 編號
// OrangePi: sys_int_ap[42] -> UART0_int (K1 SoC User Manual p.117)
// QEMU: 10
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

#define STACK_SIZE  0x1000

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
    // rdtime a0
    // ret
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

static unsigned long ticks_per_sec = 10000000UL; // 先用預設值，之後可改讀 DTB timebase-frequency
static unsigned long boot_time = 0;

/* Advanced Exercise 1: Timer Queue for one-shot timers */
struct timer_event {
    unsigned long expire_time;      /* absolute tick when timer expires */
    void (*callback)(void*);        /* callback function pointer */
    void *arg;                      /* argument passed to callback */
    struct timer_event *next;       /* linked list pointer */
};

static struct timer_event *timer_queue_head = NULL;

/* Shared payload for setTimeout callback. */
struct timeout_info {
    unsigned long cmd_time;
    char msg[128];
};

typedef void (*task_callback_t)(void *arg);

/* Advanced Exercise 2: prioritized task queue. */
// 
struct task_event {
    // callback 就是 interrput 的 handler, 然後 args 是要傳給這個 handler 的參數
    task_callback_t callback; // interrupt 任務真正要執行的函式
    void *arg; // 要傳給 callback 的參數
    int priority; // 數字越大，代表越優先先執行
    unsigned long seq; // 進 queue 的順序編號, 用來處理「priority 相同時，先進來的先執行」這件事
    struct task_event *next; // 指向下一個 task_events, 讓這些 task 串成 linked list，也就是 task queue
};

#define TASK_POOL_SIZE 64
#define TIMEOUT_INFO_POOL_SIZE 16

static struct task_event task_pool[TASK_POOL_SIZE]; // 這是 task 的靜態記憶體池。
static unsigned char task_pool_used[TASK_POOL_SIZE]; // 記錄 task_pool 哪些格子已經被用掉。
static struct task_event *task_queue_head = NULL;
static unsigned long task_seq_gen = 0;

static struct timeout_info timeout_info_pool[TIMEOUT_INFO_POOL_SIZE];
static int timeout_info_idx = 0;

static void timer_init(void) {
    // 從硬體 Generic Counter Register 讀取 tick frequency (SoC 8.2.4 p.124)
    // unsigned long counter_base = 0xD5001000UL;
    // volatile uint32_t *freq_reg = (volatile uint32_t *)(counter_base + 0x20);
    // ticks_per_sec = *freq_reg;

    unsigned long now = rdtime();
    unsigned long target = now + 2UL * ticks_per_sec; // 2 秒後
    boot_time = now;

    // sbi_set_timer(target);
    // add_timer();

    // 開 timer interrupt source: STIE (sie bit 5)
    asm volatile("csrs sie, %0" :: "r"(1UL << 5));
    // 開全域中斷: SIE (sstatus bit 1)
    asm volatile("csrsi sstatus, (1 << 1)");
}

// 這個函式是在做「暫時關閉 S-mode 的全域中斷」，同時把原本的狀態存起來，方便之後恢復。
// sstatus 裡面的 SIE: 這是 S-mode 的 global interrupt 的開關
// 讀 sstatus，清掉 SIE，存原值到 flags
static inline unsigned long local_irq_save(void) {
    unsigned long s;
    asm volatile("csrr %0, sstatus" : "=r"(s)); // 先用 csrr %0, sstatus 讀出目前的 sstatus，存到 s。
    // 關閉 global interrupt 
    asm volatile("csrc sstatus, %0" :: "r"(1UL << 1)); // 再用 csrc sstatus, 1UL << 1 把 sstatus 的 SIE bit 清掉。
    return s;
}

// 這個函式是 local_irq_save() 的配對函式，用來恢復之前保存的中斷狀態。
// 把原本的 sstatus 寫回去
static inline void local_irq_restore(unsigned long s) {
    asm volatile("csrw sstatus, %0" :: "r"(s));
}

static struct task_event *alloc_task_event(void) {
    int i;
    for (i = 0; i < TASK_POOL_SIZE; i++) {
        if (!task_pool_used[i]) {
            task_pool_used[i] = 1;
            task_pool[i].next = 0;
            return &task_pool[i];
        }
    }
    return 0;
}

static void free_task_event(struct task_event *ev) {
    int idx;
    if (!ev) return;
    idx = (int)(ev - task_pool);
    if (idx >= 0 && idx < TASK_POOL_SIZE) {
        task_pool_used[idx] = 0;
    }
}

/* Advanced Exercise 2 API */
// advance 2 的核心原則就是：
// 只有在碰到共享資料結構的臨界區時才暫時關掉 SIE
// 其他時間盡量保持中斷開著，讓系統可以繼續接收 interrupt
void add_task(task_callback_t callback, void *arg, int priority) {
    // task event struct: callback, args, priority, seq, next
    struct task_event *ev; // 要 add 的 task
    struct task_event *cur;
    unsigned long flags; // 

    if (!callback) return;
    if (priority < 0) priority = 0; // priority 只為正數, 越大優先度越高

    flags = local_irq_save(); // 在 add task 的時候，要把 SIE (global interrupt) 關閉
    ev = alloc_task_event();
    if (!ev) {
        local_irq_restore(flags);
        uart_puts("Error: task pool exhausted\n");
        return;
    }

    ev->callback = callback;
    ev->arg = arg;
    ev->priority = priority;
    ev->seq = task_seq_gen++;

    // 1. 如果 task_queue_head 是 NULL
    // 2. 如果現在新加的 task 的 priority 比目前 head 的還要高
    // 3. 如果現在新加的 task 的 priority 一樣，但是 task sequence < 目前 head 的 sequence
    // 如果發生以上三種任意一個情況，就把目前的 head 改成新加入的這個 task.
    if (!task_queue_head ||
        priority > task_queue_head->priority ||
        (priority == task_queue_head->priority && ev->seq < task_queue_head->seq)) {
        ev->next = task_queue_head;
        task_queue_head = ev;
        local_irq_restore(flags);
        return;
    }

    // 找插入的位置，因為 task queue 有按照 priority 去排序，所以要找到對應的位置插入
    cur = task_queue_head;
    while (cur->next) {
        if (priority > cur->next->priority) break;
        if (priority == cur->next->priority && ev->seq < cur->next->seq) break;
        cur = cur->next;
    }
    ev->next = cur->next;
    cur->next = ev;
    local_irq_restore(flags); // 把剛剛暫時關掉的中斷狀態，恢復回去。
}

// 缺乏「當前執行優先權」的認知。
// 要實現真正的 Preemption（搶占），內核必須記住「現在正在執行的任務優先權是多少」。
// 只有當 Queue 裡面的任務優先權 嚴格大於 (>) 當前優先權時，才能中斷當前任務。
static int current_task_priority = -1;
/* Execute queued tasks in interrupt context with SIE enabled (nested interrupt allowed). */
static void run_pending_tasks_in_interrupt(void) {
    while (1) { // 只要有 task 待執行，就一直拿一個、執行一個，直到 queue 空了才停止。
        struct task_event *ev;
        unsigned long flags;
        unsigned long sstatus_before;

        // enter critical section
        flags = local_irq_save(); // 這邊要先關閉 SIE
        // 因為 task_queue_head = ev->next 這一行在改全域的 linked list 指標。
        // 如果這時候被另一個 interrupt 打斷，而那個 interrupt 也想加 task（走 add_task() 裡也會改 task_queue_head），就會產生 race condition。
        ev = task_queue_head;

        // 要檢查新 interrupt 進來的 priority 跟現在正在進行的，誰比較高
        // 如果現在正在執行的比較高，那就不動
        if (!ev || ev->priority <= current_task_priority) {
            local_irq_restore(flags);
            break;
        }

        // 如果新進來的 interrupt priority 比較高，那就執行新的
        task_queue_head = ev->next;
        local_irq_restore(flags); 
        // leave critical section

        int previous_priority = current_task_priority;
        current_task_priority = ev->priority;

        // 
        asm volatile("csrr %0, sstatus" : "=r"(sstatus_before)); // 用 csrr（Control Status Register Read）把目前的 sstatus 讀出來，存到 sstatus_before。
        // 開啟 interrupt bit, 在跑原本的 interrupt handler 的時候，可以接收其他 interrupt.
        asm volatile("csrs sstatus, %0" :: "r"(1UL << 1)); // 用 csrs（Control Status Register Set）把 sstatus 的 SIE bit（第 1 位）設成 1。也就是打開中斷。
        ev->callback(ev->arg); // 執行 callback (也就是執行 interrupt task 對應的 handler function 並且帶入對應的 args 參數)
        asm volatile("csrw sstatus, %0" :: "r"(sstatus_before)); // 用 csrw（Control Status Register Write）把 sstatus 還原成最開始記下的狀態。

        current_task_priority = previous_priority;

        free_task_event(ev);
    }
}

/* Convert string to integer (simplified atoi) */
static int atoi(const char *str) {
    int result = 0;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return result;
}

// Advance Exercise 1: Timer Multiplexing
/* Timer pool for managing timer events */
static struct timer_event timer_pool[32];
static unsigned char timer_pool_used[32];

/* Allocate a timer event from the pool */
static struct timer_event *alloc_timer_event(void) {
    int i;
    for (i = 0; i < 32; i++) {
        if (!timer_pool_used[i]) {
            timer_pool_used[i] = 1;
            timer_pool[i].next = NULL;
            return &timer_pool[i];
        }
    }
    return NULL;
}

/* Free a timer event back to the pool */
static void free_timer_event(struct timer_event *ev) {
    int idx;
    if (!ev) return;
    idx = (int)(ev - timer_pool);
    if (idx >= 0 && idx < 32) {
        timer_pool_used[idx] = 0;
    }
}

/* Add a timer event to the queue, sorted by expire_time */
void add_timer(void (*callback)(void*), void* arg, unsigned long duration) {
    unsigned long now = rdtime();
    unsigned long expire_time = now + (duration * ticks_per_sec); // 計算到期的時間
    
    struct timer_event *new_event = alloc_timer_event();
    if (!new_event) {
        uart_puts("Error: timer pool exhausted\n");
        return;
    }
    
    // set up new timer event
    new_event->expire_time = expire_time;
    new_event->callback = callback;
    new_event->arg = arg;
    new_event->next = NULL;
    
    /* Insert sorted by expire_time */
    // 新的 timer 比 head 早，所以就是把 head 替換成新的 timer
    if (!timer_queue_head || expire_time < timer_queue_head->expire_time) { // 新的 timer event 比目前 queue 的頭還早到期
        new_event->next = timer_queue_head;
        timer_queue_head = new_event;
        /* Reprogram hardware timer to earlier time */
        sbi_set_timer(expire_time);
    } else { // 新的 timer 比 head 晚
        struct timer_event *current = timer_queue_head;
        // 從 queue head 開始往後找 找到一個位置，讓新事件插在正確的排序位置
        while (current->next && current->next->expire_time <= expire_time) {
            current = current->next;
        }
        new_event->next = current->next;
        current->next = new_event;
    }
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

        if (strncmp(header->magic, "070701", 6) != 0) { // 驗證是否為 CPIO new format
            uart_puts("Error: Invalid initramfs format.\n");
            return -1;
        }

        // hex to integer
        uint32_t namesize = (uint32_t)hextoi(header->namesize, 8);
        uint32_t filesize = (uint32_t)hextoi(header->filesize, 8);
        const char *cur_filename = ptr + sizeof(struct cpio_t);

        // CPIO 結束的標誌
        if (!strcmp(cur_filename, "TRAILER!!!")) {
            break;
        }

        uint32_t data_offset = (uint32_t)align(sizeof(struct cpio_t) + namesize, 4);
        
        // 如果找到正確的檔案
        if (!strcmp(cur_filename, filename)) {
            unsigned long target_address = (unsigned long)(ptr + data_offset);
            struct page *user_stack_page = alloc_pages(0); // 分配 user stack, 一個 (2^0) page = 4KB
            if (!user_stack_page) {
                uart_puts("Error: failed to allocate user stack.\n");
                return -1;
            }

            // 在 mem_map 裡面的第幾個 page * page 大小 (4096 Bytes)
            // user stack 的實際物理位址
            unsigned long user_stack = buddy_memory_base + (unsigned long)(user_stack_page - mem_map) * 0x1000UL; 
            // user stack pointer 的起始位址，因為 stack 是向下長的
            unsigned long user_sp = user_stack + STACK_SIZE;
            unsigned long sstatus;

            // 從 CSR sstatus 讀出目前的值，存到 C 變數 sstatus。
            // csrr a0, sstatus
            asm volatile("csrr %0, sstatus" : "=r"(sstatus));
            // go check risc-v manual (4.1.1 p.740)
            // 把 sstatus 第 8 bit 清成 0。 (bitwise NOT) 
            // SPP = 0 代表 sret 後要回到 U-mode。
            // The SPP bit indicates the privilege level at which a hart was executing before entering supervisor mode.
            sstatus &= ~(1UL << 8);
            // 把 sstatus 第 5 bit 設成 1。
            // The SPIE bit indicates whether supervisor interrupts were enabled prior to trapping into supervisor mode.
            // 在進入 trap（異常/中斷）之前，SIE 的狀態
            sstatus |= (1UL << 5);

            // 開始跳到 u-mode 執行檔案
            asm volatile(
                "csrw sepc, %0\n" // sepc = target_address
                "csrw sstatus, %1\n" // sstatus = sstatus
                "csrw sscratch, sp\n" // 把目前 S-mode 的 stack pointer 存到 sscratch
                "mv sp, %2\n" // sp = user_sp
                "sret\n" // 從 s mode 跳進 U-mode (riscv 的 assembly 語法)
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

/* Callback function for setTimeout & addTask */
void timeout_callback(void *arg) {
    struct timeout_info *info = (struct timeout_info *)arg;
    
    unsigned long current_time = rdtime();
    unsigned long elapsed = (current_time - info->cmd_time) / ticks_per_sec;
    unsigned long current_seconds = current_time / ticks_per_sec;
    
    uart_puts("[Timeout] Message: ");
    uart_puts(info->msg);
    uart_puts(" (elapsed: ");
    
    /* Print elapsed in decimal */
    if (elapsed == 0) {
        uart_putc('0');
    } else {
        char buf[32];
        int pos = 0;
        unsigned long temp = elapsed;
        while (temp > 0) {
            buf[pos++] = '0' + (temp % 10);
            temp /= 10;
        }
        while (pos-- > 0) {
            uart_putc(buf[pos]);
        }
    }
    
    uart_puts("s, current_time: ");
    
    /* Print current_seconds in decimal */
    if (current_seconds == 0) {
        uart_putc('0');
    } else {
        char buf[32];
        int pos = 0;
        unsigned long temp = current_seconds;
        while (temp > 0) {
            buf[pos++] = '0' + (temp % 10);
            temp /= 10;
        }
        while (pos-- > 0) {
            uart_putc(buf[pos]);
        }
    }
    
    uart_puts("s)\n");
}

/* Periodic boot-time task scheduled via add_timer */
void boot_time_task(void *arg) {
    unsigned long now = rdtime();
    unsigned long seconds = 0;

    if (now >= boot_time) {
        seconds = (now - boot_time) / ticks_per_sec;
    }

    uart_puts("boot time: ");
    if (seconds == 0) {
        uart_putc('0');
    } else {
        char digits[32];
        int digit_count = 0;
        while (seconds > 0 && digit_count < (int)sizeof(digits)) {
            digits[digit_count++] = '0' + (seconds % 10);
            seconds /= 10;
        }
        while (digit_count-- > 0) {
            uart_putc(digits[digit_count]);
        }
    }
    uart_puts("\n");

    /* reschedule itself in 2 seconds */
    // add_timer(boot_time_task, NULL, 2UL);
}

// lab4 basic exercise 1 跟 2 跟 3 都會到 do_trap 這邊處理
// 所以在 do_trap 可能就要區分一下是哪一個
void do_trap(struct pt_regs *regs) {
    unsigned long scause = regs->scause;
    
    unsigned long is_interrupt = scause >> ((sizeof(unsigned long) * 8) - 1); // 8 * 8 - 1 = 63 為了兼容 rv32 rv64
    // 取 scause 的 MSB, 代表 interrupt bit, 是 1 的話 代表是 interrupt, 是 0 的話代表是 exception

    unsigned long cause_code = scause & 0xffUL; // 取 scause 的最低的 8 位
    // 如果 cause_code = 5 就代表是 timer interrupt

    // riscv spec 4.1.8 p.747 Supervisor Cause Register
    // scause = 5 代表 supervisor timer interrupt
    if (is_interrupt && cause_code == 5UL) {// exercise 2: supervisor timer interrupt
        unsigned long now = rdtime();
        /* boot time printing moved to a scheduled task (boot_time_task)
         * Top-half now only handles timer queue dequeueing and reprogramming.
         */

        /* Lab 4 Advanced Exercise 1: enqueue expired timer callbacks as tasks */
        // Top-half
        while (timer_queue_head && timer_queue_head->expire_time <= now) {
            struct timer_event *expired = timer_queue_head;
            timer_queue_head = expired->next;

            if (expired->callback) {
                add_task(expired->callback, expired->arg, 1);
            }
            
            /* Free the expired timer event back to pool */
            free_timer_event(expired);
        }

        /* Advanced Exercise 2: execute tasks before leaving interrupt context. */
        run_pending_tasks_in_interrupt(); // 
        
        /* Reprogram hardware timer to next event or default 2 seconds */
        unsigned long next_target = timer_queue_head ? timer_queue_head->expire_time : (now + 2UL * ticks_per_sec);
        sbi_set_timer(next_target);
        return;
    }

    // riscv spec 4.1.8 p.747 Supervisor Cause Register
    // 反正只要 scause_code = 9 就代表是 supervisor external interrupt (SEIP)
    // 而 SEIP 一定是從 PLIC 來的
    // 除了 software exception 跟 timer interrupt 其他都是從 PLIC 來的
    if (is_interrupt && cause_code == 9UL) { 
        uart_plic_handle_interrupt();
        run_pending_tasks_in_interrupt();
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
        // sepc: 儲存發生 trap 當下的程式執行的位址
    }
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

int priority_set[4];

void p1_callback(){
    uart_puts("P1 start\n");
    uart_puts("P1 end\n");
}

void p3_callback(){
    uart_puts("P3 start\n");
    add_task(p1_callback, NULL, priority_set[0]);
    add_timer(NULL, NULL, 0);
    uart_puts("P3 end\n");
}

void p2_callback(){
    uart_puts("P2 start\n");
    add_task(p3_callback, NULL, priority_set[2]);
    add_timer(NULL, NULL, 0);
    uart_puts("P2 end\n");
}

void p4_callback(){
    uart_puts("P4 start\n");
    add_task(p2_callback, NULL, priority_set[1]);
    add_timer(NULL, NULL, 0);
    uart_puts("P4 end\n");
}

// p4 start
// p4 end
// p2 start
// p3 start 
// p3 end
// p2 end 
// p1 start
// p1 end

void test_func(){
    int from_small_to_big = 1; // set to 0 if the task with a smaller number has a higher priority
    if(from_small_to_big){
        priority_set[0] = 10;
        priority_set[1] = 20;
        priority_set[2] = 30;
        priority_set[3] = 40;
    }else{
        priority_set[0] = 40;
        priority_set[1] = 30;
        priority_set[2] = 20;
        priority_set[3] = 10;
    }

    add_task(p4_callback, NULL, priority_set[3]);
}


extern void mm_init(unsigned long base, unsigned long size);
extern void memory_reserve(unsigned long base, unsigned long size);
extern void buddy_test();
extern void test_alloc_1();

extern char _start[];
extern char _end[];

void start_kernel(unsigned long hartid) {
    boot_cpu_hartid = hartid;
    devicetree_early_init(fdt_ptr);
    timer_init();
    /* initialize UART ring buffers before any UART I/O */
    uart_buffer_init();
    /* enable UART RX and TX interrupts at UART peripheral (PLIC not configured yet) */
    // 讓 uart 本身可以發送中斷
    uart_enable_irqs(1, 1);
    /* configure PLIC for UART interrupt on this hart */
    // 把 uart 的 interrupt 導到對應的 hart
    uart_plic_init_for_hart();
    /* enable supervisor external interrupt delivery */
    // s-mode 開啟 external interrupt enable
    uart_enable_external_interrupt();
    
    // 啟動記憶體管理系統 (mm_init)
    // 原本寫 if(mem_base & mem_size) 這樣會有問題
    // 因為 orangepi 的 base 就是 00000000 C 會認為那是 false 就沒有去執行 mm_init()
    if (mem_size > 0) {
        uart_puts("Memory Base: ");
        uart_hex(mem_base);
        uart_puts(", Size: ");
        uart_hex(mem_size);
        uart_puts("\n");
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

    /* Schedule periodic boot-time print via add_timer (every 2 seconds) */
    // add_timer(boot_time_task, NULL, 2UL);
    add_timer(test_func, NULL, 0);

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
            uart_puts("help      : print this help menu\n");
            uart_puts("hello     : print Hello World!\n");
            uart_puts("info      : print system information\n");
            uart_puts("load      : load kernel image from host computer\n");
            uart_puts("ls        : list files in initramfs\n");
            uart_puts("cat       : display file content in initramfs\n");
            uart_puts("buddy     : test the buddy system memory allocator\n");
            uart_puts("test_lab3 : test the memory allocator (test_alloc_1)\n");
            uart_puts("setTimeout: set a one-shot timer (setTimeout SECONDS MESSAGE)\n");
            uart_puts("addTask   : add a priority task (addTask PRIORITY MESSAGE)\n");
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
                kernel_entry(0, fdt_ptr); // a0 & a1 register 
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
        else if (strncmp(buf, "setTimeout ", 11) == 0) {
            /* Advanced Exercise 1: setTimeout SECONDS MESSAGE */
            char *args = buf + 11;
            int seconds = atoi(args);
            
            /* Skip to MESSAGE part */
            // setTimeout 15 hello world 1
            while (*args && *args != ' ') args++;
            while (*args && *args == ' ') args++;
            
            char *message = args;
            
            if (seconds <= 0 || *message == '\0') {
                uart_puts("Usage: setTimeout SECONDS MESSAGE\n");
            } else {
                if (timeout_info_idx < TIMEOUT_INFO_POOL_SIZE) {
                    struct timeout_info *info = &timeout_info_pool[timeout_info_idx++];
                    info->cmd_time = rdtime();
                    strncpy(info->msg, message, 127);
                    info->msg[127] = '\0';
                    
                    add_timer(timeout_callback, (void*)info, seconds);
                    uart_puts("Timer set for ");
                    uart_hex((unsigned long)seconds);
                    uart_puts(" seconds\n");
                } else {
                    uart_puts("Error: timeout info pool exhausted\n");
                }
            }
        }
        else if (strncmp(buf, "addTask ", 8) == 0) {
            char *args = buf + 8;
            int priority = atoi(args);

            while (*args && *args != ' ') args++;
            while (*args && *args == ' ') args++;

            if (*args == '\0') {
                uart_puts("Usage: addTask PRIORITY MESSAGE\n");
            } else {
                struct timeout_info *info;
                if (timeout_info_idx >= TIMEOUT_INFO_POOL_SIZE) {
                    uart_puts("Error: timeout info pool exhausted\n");
                } else {
                    info = &timeout_info_pool[timeout_info_idx++];
                    info->cmd_time = rdtime();
                    strncpy(info->msg, args, 127);
                    info->msg[127] = '\0';
                    add_task(timeout_callback, info, priority);
                    uart_puts("Task queued with priority ");
                    uart_hex((unsigned long)priority);
                    uart_puts("\n");
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
