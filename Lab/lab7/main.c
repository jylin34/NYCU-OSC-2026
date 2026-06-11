#include <stdint.h>
#include <stddef.h>
#include "vfs.h"
#include "tmpfs.h"

extern int vfs_lookup(const char* pathname, struct vnode** target);

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
extern char uart_polling_getc(void);
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
unsigned long PLIC_BASE = 0xE0000000UL;
// Qemu: 0xc000000 Orangepi: 0xE0000000 ?
unsigned long UART_PLIC_IRQ = 42UL;
// UART 這個裝置 在 PLIC 裡面對應的 interrupt 編號
// OrangePi: sys_int_ap[42] -> UART0_int (K1 SoC User Manual p.117)
// QEMU: 10
extern void * allocate(size_t size);
extern void free(void * ptr);
extern void * slab_alloc(size_t size); // kmalloc
extern void slab_free(void * ptr);
extern struct page* alloc_pages(int order);
extern void free_pages(struct page* p);
extern struct page* mem_map;
extern unsigned long buddy_memory_base;

struct task_struct;
struct task_struct* get_current(void);

#ifndef PAGE_OFFSET
#define PAGE_OFFSET 0xffffffc000000000UL
#endif

/* Ensure user page protections and stack top are available early for exec() */
#ifndef PAGE_USER_CODE
#define PAGE_USER_CODE  (1 << 0 | 1 << 1 | 1 << 3 | 1 << 4 | 1 << 6 | 1 << 7)
#endif
#ifndef PAGE_USER_STACK
#define PAGE_USER_STACK (1 << 0 | 1 << 1 | 1 << 2 | 1 << 4 | 1 << 6 | 1 << 7)
#endif

#ifndef PROT_READ
#define PROT_READ  1
#endif
#ifndef PROT_WRITE
#define PROT_WRITE 2
#endif
#ifndef PROT_EXEC
#define PROT_EXEC  4
#endif
#ifndef USER_STACK_TOP
#define USER_STACK_TOP 0x0000004000000000UL

/* mmap base when no hint provided */
#define MMAP_BASE 0x0000000000100000UL
#endif

#ifndef USER_STACK_SIZE
#define USER_STACK_SIZE (STACK_SIZE * 2)
#endif

#ifndef SATP_SV39
#define SATP_SV39 (8UL << 60)
#endif

#ifndef MAKE_SATP
#define MAKE_SATP(pgd_pa) (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))
#endif

extern unsigned long pgd[];

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

struct page {
    struct list_head list;
    int order;
    int refcount; // 表示這個 page 目前被幾個 process 使用
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

static unsigned long ticks_per_sec = 24000000UL; // 先用預設值，之後可改讀 DTB timebase-frequency
static unsigned long boot_time = 0;

/* Advanced Exercise 1: Timer Queue for one-shot timers */
struct timer_event {
    unsigned long expire_time;      /* absolute tick when timer expires */
    void (*callback)(void*);        /* callback function pointer */
    void *arg;                      /* argument passed to callback */
    struct timer_event *next;       /* linked list pointer */
};

static struct timer_event *timer_queue_head = NULL;
static void free_timer_event(struct timer_event *ev);

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
static unsigned long next_sched_tick = 0;
static unsigned long timer_time_slice = 0;

static void timer_rearm(void) {
    unsigned long now = rdtime();

    if (timer_time_slice == 0) {
        timer_time_slice = ticks_per_sec / 32;
    }

    if (next_sched_tick == 0) {
        next_sched_tick = now + timer_time_slice;
    }

    while (next_sched_tick <= now) {
        next_sched_tick += timer_time_slice;
    }

    sbi_set_timer(next_sched_tick);
}

static void timer_run_due_events(void) {
    unsigned long now = rdtime();

    while (timer_queue_head && timer_queue_head->expire_time <= now) {
        struct timer_event *ev = timer_queue_head;
        timer_queue_head = ev->next;

        if (ev->callback) {
            ev->callback(ev->arg);
        }

        free_timer_event(ev);
        now = rdtime();
    }
}

static void timer_init(void) {
    unsigned long now = rdtime();
    unsigned long time_slice = ticks_per_sec / 32;

    boot_time = now;
    timer_time_slice = time_slice;
    next_sched_tick = now + time_slice;

    sbi_set_timer(next_sched_tick);

    // unsigned long target = now + 2UL * ticks_per_sec; // 2 秒後
    // boot_time = now;
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
        unsigned long target = (expire_time < next_sched_tick) ? expire_time : next_sched_tick;
        sbi_set_timer(target);
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

// --- 補上這段：自製 C 標準函式庫 ---

void *memcpy(void *dest, const void *src, unsigned long n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *s, int c, unsigned long n) {
    char *p = (char *)s;
    while (n--) {
        *p++ = (char)c;
    }
    return s;
}
// ------------------------------------

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



/* Page protection bits */
#define PTE_V  (1 << 0) // PAGE_PRESENT
#define PTE_R  (1 << 1) // PAGE_READ
#define PTE_W  (1 << 2) // PAGE_WRITE
#define PTE_X  (1 << 3) // PAGE_EXEC
#define PTE_U  (1 << 4) // PAGE_USER
#define PTE_G  (1 << 5) // PAGE_GLOBAL
#define PTE_A  (1 << 6) // PAGE_ACCESSED
#define PTE_D  (1 << 7) // PAGE_DIRTY

#define PAGE_USER_CODE  (PTE_V | PTE_R | PTE_X | PTE_U | PTE_A | PTE_D)  

enum task_state {
    TASK_RUNNING,
    TASK_RUNNABLE,
    TASK_ZOMBIE
};

/* Per-process virtual memory area (VMA) descriptor */
struct vma {
    unsigned long start; /* start VA */
    unsigned long len;   /* length in bytes */
    int prot;            /* protection bits */
    int flags;           /* mmap flags (MAP_ANONYMOUS / MAP_POPULATE) */ 
    struct vma *next; // singly linked list
};

struct task_struct {
    struct thread_struct {
        unsigned long ra; // return address, 
        unsigned long sp; // stack pointer, 
        unsigned long s[12];
    } thread;
    int pid;
    enum task_state state;
    unsigned long kernel_sp;
    unsigned long user_sp;
    unsigned long stack; // kernel stack
    struct task_struct* next;
    // Lab5 : Advance Exercise - POSIX Signal
    void (*handlers[32])(); // map signal number to corresponding handler address
    uint32_t pending_signals; // 紀錄目前等待處理的 signal
    struct pt_regs saved_context; // the user context before signal interrupt
    int is_handling_signal; // whether it is handling signal now
    void *signal_stack_base;
    // Lab6 ---------------------------------
    unsigned long user_code_src; // demand paging code backing address in initramfs
    unsigned long user_code_size; // code image size in bytes
    unsigned long *user_code_phys_pages; // 這邊存著每一個 code page 對應的 physical address
    unsigned long user_code_pages; // code section 一共用了幾個 pages
    unsigned long *user_pgd; // 每一個 Process 都有自己的 PGD
    unsigned long user_stack_phys_base; // user stack 的實體 base (for free/clone)
    struct vma *vm_list; /* per-process VMA list head */
    // Lab7: Basic Exercise 3 - Multitask VFS, working directory -----------
    struct vnode* cwd;          // 當前工作目錄的 vnode
    struct file* fd_table[16];  // Each process should have a file descriptor table to bookkeep the opened files.
};

struct task_queue;
static struct task_queue zombie_queue;
static void enqueue(struct task_queue* q, struct task_struct* task);

// 把一個新的 struct vma *v 插入到以 start（虛擬位址）由小到大排序的 VMA 鏈表中，維持鏈表順序。
static void vma_insert_sorted(struct vma **head, struct vma *v) {
    if (!v) return;
    if (!*head || v->start < (*head)->start) {
        v->next = *head;
        *head = v;
        return;
    }
    struct vma *cur = *head;
    while (cur->next && cur->next->start < v->start) cur = cur->next;
    v->next = cur->next;
    cur->next = v;
}

/* remove a VMA that matches start and len exactly. Returns 1 if removed, 0 otherwise. */
static int vma_remove(struct vma **head, unsigned long start, unsigned long len) {
    if (!head || !*head) return 0;
    struct vma *cur = *head;
    struct vma *prev = NULL;
    while (cur) {
        if (cur->start == start && cur->len == len) {
            if (prev) prev->next = cur->next;
            else *head = cur->next;
            free(cur);
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }
    return 0;
}

/* Convert prot bits (PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4) to PTE flags. */
static unsigned long prot_to_pte_flags(int prot) {
    unsigned long flags = 0;
    if (prot & 1) flags |= PTE_R;
    if (prot & 2) flags |= PTE_W;
    if (prot & 4) flags |= PTE_X;
    /* always user + accessed + dirty + present */
    flags |= PTE_U | PTE_A | PTE_D | PTE_V;
    return flags;
}

// 在以 start 升冪排序的 VMA 鏈表中，尋找第一個與區間 [start, end) 有交集的 VMA；若找到回傳該 struct vma*，找不到回傳 NULL。
static struct vma *vma_find_overlap(struct vma *head, unsigned long start, unsigned long end) {
    struct vma *cur = head;
    while (cur) {
        unsigned long a = cur->start;
        unsigned long b = cur->start + cur->len;
        if (end > a && start < b) return cur;
        cur = cur->next;
    }
    return NULL;
}

// 在已排序的 vm_list 中找一塊長度為 len 的空洞（或精確 hint），回傳起始 VA（失敗回 0）。
// 目前 curr->vm_list 裡沒有被任何 VMA 佔用的一段區間
// hint 是使用者 sys_mmap 傳進來的 starting address
static unsigned long vma_find_free_range(struct vma *head, unsigned long len, unsigned long hint, int exact) {
    unsigned long align = 4096UL;
    const unsigned long UMAX = ~0UL;

    if (len == 0) return 0;
    /* align length up to page */
    unsigned long llen = (len + align - 1) & ~(align - 1);

    /* clamp hint */
    if (hint && hint < MMAP_BASE) hint = MMAP_BASE;

    /* exact hint */
    if (exact && hint) {
        if (hint & (align - 1)) return 0; // 代表沒有對齊 4096
        if (hint > UMAX - llen) return 0; /* overflow */
        unsigned long end = hint + llen;
        if (!vma_find_overlap(head, hint, end)) return hint;
        return 0;
    }

    unsigned long candidate = (hint && hint >= MMAP_BASE) ? ((hint + align - 1) & ~(align - 1)) : MMAP_BASE;
    if (!head) return candidate;

    unsigned long prev_end = MMAP_BASE; // 從使用者可用的最低位址開始搜尋空間。
    struct vma *cur = head;
    // 遍歷每一個 VMA，看哪一塊 VMA 放得下使用者要求的 mmap
    while (cur) {
        /* try to place between prev_end and cur->start */
        unsigned long gap_start = prev_end;
        unsigned long gap_end = cur->start;
        unsigned long start = candidate > gap_start ? candidate : gap_start;

        if (start > UMAX - llen) {
            /* would overflow */
        } else {
            unsigned long end = start + llen;
            if (end <= gap_end) return start;
        }

        /* compute prev_end = cur->start + cur->len with overflow guard */
        if (cur->start > UMAX - cur->len) prev_end = UMAX;
        else prev_end = cur->start + cur->len;
        cur = cur->next;
    }

    /* after last vma: ensure we don't grow into stack region */
    if (USER_STACK_TOP <= align) return 0;
    unsigned long max_end = USER_STACK_TOP - align; /* leave a guard page */
    if (prev_end > UMAX - llen) return 0;
    if (prev_end + llen <= max_end) return prev_end;
    return 0;
}

#define USER_PGD_INDEX(va) (((unsigned long)(va) >> 30) & 0x1FF)
#define USER_PMD_INDEX(va) (((unsigned long)(va) >> 21) & 0x1FF)
#define USER_PTE_INDEX(va) (((unsigned long)(va) >> 12) & 0x1FF)

// 在給定 top-level PGD 和 VA 的情況下，找到對應的 PTE 記憶體位址並回傳指標
static unsigned long *user_pte_entry(unsigned long *root_pgd, unsigned long va) {
    unsigned long *pgd_entry;
    unsigned long *pmd_table;
    unsigned long *pmd_entry;
    unsigned long *pte_table;
    unsigned long *pte_entry;
    unsigned long pmd_pa;
    unsigned long pte_pa;

    if (!root_pgd) return NULL;

    pgd_entry = &root_pgd[USER_PGD_INDEX(va)];
    if (!(*pgd_entry & PTE_V)) return NULL;

    pmd_pa = (((unsigned long)(*pgd_entry)) >> 10) << 12;
    pmd_table = (unsigned long *)(pmd_pa + PAGE_OFFSET);
    pmd_entry = &pmd_table[USER_PMD_INDEX(va)];
    if (!(*pmd_entry & PTE_V)) return NULL;

    pte_pa = (((unsigned long)(*pmd_entry)) >> 10) << 12;
    pte_table = (unsigned long *)(pte_pa + PAGE_OFFSET);
    pte_entry = &pte_table[USER_PTE_INDEX(va)];
    if (!(*pte_entry & PTE_V)) return NULL;

    return pte_entry;
}

static void vma_free_list(struct vma *head) {
    while (head) {
        struct vma *next = head->next;
        free(head);
        head = next;
    }
}

// 
static struct vma *vma_clone_list(struct vma *head) {
    struct vma *new_head = NULL;
    struct vma *tail = NULL;

    while (head) {
        struct vma *node = (struct vma *)allocate(sizeof(struct vma));
        if (!node) {
            vma_free_list(new_head);
            return NULL;
        }

        *node = *head;
        node->next = NULL;

        if (!new_head) {
            new_head = node;
            tail = node;
        } else {
            tail->next = node;
            tail = node;
        }

        head = head->next;
    }

    return new_head;
}

static int vma_is_code_region(struct vma *v) {
    return v && v->start == 0UL && v->len > 0;
}

static int vma_is_stack_region(struct vma *v) {
    return v && v->start == (USER_STACK_TOP - USER_STACK_SIZE) && v->len == USER_STACK_SIZE;
}

static struct vma *vma_find_by_addr(struct vma *head, unsigned long addr) {
    return vma_find_overlap(head, addr, addr + 1);
}

static void terminate_current_process(void) {
    struct task_struct *curr = get_current();
    if (!curr) return;

    uart_puts("[Segmentation fault]: Kill Process\n");
    curr->state = TASK_ZOMBIE;
    enqueue(&zombie_queue, curr);
    schedule();
}

static int handle_user_page_fault(struct pt_regs *regs) {
    struct task_struct *curr = get_current();
    struct vma *v; // 
    unsigned long fault_addr;
    unsigned long page_va;
    unsigned long *pte;
    struct page *pg;
    unsigned long pa;
    unsigned long pte_flags;
    unsigned long copy_len;

    // segmentation fault
    if (!curr || !curr->user_pgd || !curr->vm_list) {
        terminate_current_process();
        return -1;
    }

    // segmentation fault
    fault_addr = regs->stval; // 觸發 page‑fault 的 VA
    v = vma_find_by_addr(curr->vm_list, fault_addr);
    if (!v) {
        terminate_current_process();
        return -1;
    }

    // 這個 VA 對應的 page 還沒有被 map 好
    uart_puts("[Translation fault]: ");
    uart_hex(fault_addr);
    uart_puts("\n");

    page_va = fault_addr & ~(4096UL - 1); // align page
    pte = user_pte_entry(curr->user_pgd, page_va); // 查詢 VA 對應的 PTE (return 是一個 pointer)
    if (pte && (*pte & PTE_V)) { // PTE_V 是在 pagewalk() 這邊被設定成 1 的
        if (regs->scause == 15UL) { // load page fault
            uart_puts("[Permission fault]: ");
            uart_hex(fault_addr);
            uart_puts("\n");
        }

        // load/store page fault, 這個位址所屬的 VMA 允許寫入，但目前對應的 PTE 還沒有寫入權限時
        // CoW 寫入的時候才複製
        if (regs->scause == 15UL && (v->prot & PROT_WRITE) && !(*pte & PTE_W)) {
            unsigned long old_pte = *pte;
            unsigned long old_pa = (((unsigned long)(*pte)) >> 10) << 12;
            struct page *old_page = mem_map + ((old_pa - buddy_memory_base) / 4096UL);
            unsigned long new_flags;

            // 代表這個 page 只有自己在用
            if (old_page->refcount <= 1) {
                *pte = old_pte | PTE_W | PTE_A | PTE_D; // 直接在原本的 PTE 加上寫權限。
                asm volatile("sfence.vma zero, zero\n" ::: "memory");
                return 0;
            }

            // 分配一個新的 page
            pg = alloc_pages(0);
            if (!pg) {
                terminate_current_process();
                return -1;
            }

            pa = buddy_memory_base + (unsigned long)(pg - mem_map) * 4096UL;
            // 把原本 shared page 的內容複製到這個新頁。
            memcpy((void *)(pa + PAGE_OFFSET), (const void *)(old_pa + PAGE_OFFSET), 4096UL);
            new_flags = (old_pte | PTE_W | PTE_A | PTE_D) & 0x3FFUL;
            *pte = ((((pa - buddy_memory_base) >> 12) << 10) | new_flags); // 把目前這個 process 的頁表項改成指向這個新頁。
            free_pages(old_page); // 舊頁少了一個使用者，引用計數要下降。
            asm volatile("sfence.vma zero, zero\n" ::: "memory"); // CPU 刷新 TLB
            return 0;
        }

        // load page fault, 但是這個 page 不允許寫入
        if (regs->scause == 15UL && !(v->prot & PROT_WRITE)) {
            terminate_current_process();
            return -1;
        }

        terminate_current_process();
        return -1;
    }

    // 接下來是處理 demand paging 的部分
    pg = alloc_pages(0);
    if (!pg) {
        terminate_current_process();
        return -1;
    }

    pa = buddy_memory_base + (unsigned long)(pg - mem_map) * 4096UL;
    pte_flags = prot_to_pte_flags(v->prot);

    if (vma_is_code_region(v)) {
        unsigned long offset = page_va - v->start; // v->start 代表這個 vma 的開頭
        unsigned long src;

        // 沒有保存可供載入的 user code 原始資料
        // or 要載入的那一頁已經超過程式映像的範圍，沒有對應內容可讀
        if (!curr->user_code_src || offset >= curr->user_code_size) {
            free_pages(pg);
            terminate_current_process();
            return -1;
        }

        src = curr->user_code_src + offset;
        copy_len = curr->user_code_size - offset;
        if (copy_len > 4096UL) copy_len = 4096UL; // 最多只複製一個 page 的大小，也就是 4096 bytes

        // 從 source copy length 長度的資料到 destination
        memcpy((void *)(pa + PAGE_OFFSET), (const void *)src, copy_len); // destination, source, length
        if (copy_len < 4096UL) {
            memset((void *)(pa + PAGE_OFFSET + copy_len), 0, 4096UL - copy_len);
        }
    } else { // stack section, or other non-code section (eg. anonymous mmap)
        memset((void *)(pa + PAGE_OFFSET), 0, 4096UL);
    }

    map_pages(curr->user_pgd, page_va, 4096UL, pa, pte_flags); // pgd, va, size, pa, proto
    asm volatile("sfence.vma zero, zero\n" ::: "memory");
    return 0;
}

static void free_task_user_space(struct task_struct *task) {
    struct vma *v;

    if (!task) return;

    for (v = task->vm_list; v; v = v->next) {
        unsigned long va;

        if (!task->user_pgd) {
            continue;
        }

        for (va = v->start; va < v->start + v->len; va += 4096UL) {
            unsigned long *pte = user_pte_entry(task->user_pgd, va);
            if (!pte || !(*pte & PTE_V)) continue;

            unsigned long pa = (((unsigned long)(*pte)) >> 10) << 12;
            struct page *page = mem_map + ((pa - buddy_memory_base) / 4096UL);
            free_pages(page);
            *pte = 0;
        }
    }

    if (task->user_code_phys_pages) {
        free(task->user_code_phys_pages);
        task->user_code_phys_pages = NULL;
        task->user_code_pages = 0;
    }

    task->user_stack_phys_base = 0;

    if (task->user_pgd) {
        free(task->user_pgd);
        task->user_pgd = NULL;
    }

    vma_free_list(task->vm_list);
    task->vm_list = NULL;
}

extern unsigned long *alloc_user_pgd(void);

// tp: 這個 register 會指向目前正在執行的 thread 的 pcb
struct task_struct* get_current() {
    // uart_puts("[Kernel][get_current] Start\n");

    register struct task_struct* current asm("tp");

    // uart_puts("[get_current] tp=0x");
    // uart_hex((unsigned long)current);
    // uart_puts("\n");

    return current;
}


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
            struct task_struct *curr = get_current();
            if (!curr) {
                uart_puts("Error: no current task.\n");
                return -1;
            }

            unsigned long *user_pgd = alloc_user_pgd();
            if (!user_pgd) {
                uart_puts("Error: failed to allocate user pgd.\n");
                return -1;
            }

            curr->user_pgd = user_pgd;
            curr->vm_list = NULL;
            curr->user_code_src = target_address;
            curr->user_code_size = filesize;
            curr->user_code_phys_pages = NULL;
            curr->user_code_pages = 0;
            curr->user_stack_phys_base = 0;
            curr->user_sp = USER_STACK_TOP;

            /* Temporary debug: verify anonymous mmap can write/read after user_pgd exists. */
            mmap_w();
            mmap_r();

            uart_puts("\n[Kernel][exec] get_current()");

            /* Lazy setup: keep only VMA metadata; actual pages will be allocated on page fault. */
            unsigned long pages = (filesize + 4096UL - 1) / 4096UL;
            if (pages == 0) pages = 1;

            /* create VMA entries for code and stack (metadata only) */
            struct vma *code_vma = (struct vma *)allocate(sizeof(struct vma));
            if (code_vma) {
                code_vma->start = 0UL;
                code_vma->len = pages * 4096UL;
                code_vma->prot = PROT_READ | PROT_EXEC;
                code_vma->flags = 0;
                code_vma->next = NULL;
                vma_insert_sorted(&curr->vm_list, code_vma);
            }

            struct vma *stack_vma = (struct vma *)allocate(sizeof(struct vma));
            if (stack_vma) {
                stack_vma->start = USER_STACK_TOP - USER_STACK_SIZE;
                stack_vma->len = USER_STACK_SIZE;
                stack_vma->prot = PROT_READ | PROT_WRITE;
                stack_vma->flags = 0;
                stack_vma->next = NULL;
                vma_insert_sorted(&curr->vm_list, stack_vma);
            }

            // Lab7: Advance Exercise 1 - /dev/uart
            struct file* uart_file = NULL;
            if (vfs_open("/dev/uart", 0, &uart_file) == 0) {
                // 若之前有開啟過檔案，要先 close 以免洩漏
                if (curr->fd_table[0]) vfs_close(curr->fd_table[0]);
                if (curr->fd_table[1]) vfs_close(curr->fd_table[1]);
                if (curr->fd_table[2]) vfs_close(curr->fd_table[2]);
                curr -> fd_table[0] = uart_file; // stdin
                curr -> fd_table[1] = uart_file; // stdout
                curr -> fd_table[2] = uart_file; // stderr
                uart_file->refcount += 2; // 因為 fd_table 有三個位置都指向它，所以引用計數 +2
            }
            // ====================================

            /* 3) switch satp and enter U-mode */
            unsigned long pgd_pa = (unsigned long)curr->user_pgd - PAGE_OFFSET;
            unsigned long satp = MAKE_SATP(pgd_pa);
            asm volatile("csrw satp, %0" :: "r"(satp) : "memory");
            asm volatile("sfence.vma\n" ::: "memory");

            uart_puts("\n[Kernel][exec] Finish Setting up satp register to new user program's pgd");

            unsigned long sstatus;
            asm volatile("csrr %0, sstatus" : "=r"(sstatus));
            sstatus &= ~(1UL << 8); /* SPP = 0 -> U-mode after sret */
            sstatus |= (1UL << 5);  /* SPIE = 1 */

            uart_puts("\n[Kernel][exec] Finish Setting up sstatus register\n");

            /* entry_va is the user-mode entry point (virtual) where code was mapped.
             * We mapped program pages starting at VA 0x0, so use 0x0 here. */
            unsigned long entry_va = 0x0UL;

            asm volatile(
                "csrw sepc, %0\n"
                "csrw sstatus, %1\n"
                "csrw sscratch, sp\n"
                "mv sp, %2\n"
                "sret\n"
                :
                : "r"(entry_va), "r"(sstatus), "r"(curr->user_sp)
                : "memory");

            uart_puts("\n[Kernel][exec] Execute user program successfully");

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

/* forward declarations */
void syscall_handler(struct pt_regs *regs);
void schedule(void);
void check_signal(struct task_struct *task, struct pt_regs *regs);

// lab4 basic exercise 1 跟 2 跟 3 都會到 do_trap 這邊處理
// 所以在 do_trap 可能就要區分一下是哪一個
void do_trap(struct pt_regs *regs) {
    unsigned long scause = regs->scause;
    
    unsigned long is_interrupt = scause >> ((sizeof(unsigned long) * 8) - 1); // 8 * 8 - 1 = 63 為了兼容 rv32 rv64
    // 取 scause 的 MSB, 代表 interrupt bit, 是 1 的話 代表是 interrupt, 是 0 的話代表是 exception

    unsigned long cause_code = scause & 0xffUL; // 取 scause 的最低的 8 位
    // 如果 cause_code = 5 就代表是 timer interrupt

    if (is_interrupt) {
        // riscv spec 4.1.8 p.747 Supervisor Cause Register
        // scause = 5 代表 supervisor timer interrupt
        if (cause_code == 5UL) { // exercise 2: supervisor timer interrupt
            unsigned long now = rdtime();
            unsigned long next_val = now + (ticks_per_sec / 32); 
            sbi_set_timer(next_val);
            // timer_rearm();            
            schedule();
        } else if (cause_code == 9UL) {
            // riscv spec 4.1.8 p.747 Supervisor Cause Register
            // 反正只要 scause_code = 9 就代表是 supervisor external interrupt (SEIP)
            // 而 SEIP 一定是從 PLIC 來的
            // 除了 software exception 跟 timer interrupt 其他都是從 PLIC 來的
            uart_plic_handle_interrupt();
            run_pending_tasks_in_interrupt();
        }
    } else {
        if (regs->scause == 8UL) { // system call
            regs->sepc += 4; // 這段是在處理「U-mode 的 ecall」後，避免無限重複 trap。
            // sepc: 儲存發生 trap 當下的程式執行的位址
            syscall_handler(regs);
        }
        // Instruction Page Fault (12): CPU 嘗試從該虛擬位址抓取指令時發生的 page-fault。
        // Load Page Fault (13): 在執行 load/read 記憶體操作時發生的 page-fault。
        // Store Page Fault (15): 在執行 store 或原子寫入（AMO）時發生的 page-fault。常用於偵測寫入到只讀頁（CoW 情境
        else if (regs->scause == 12UL || regs->scause == 13UL || regs->scause == 15UL) {
            if (handle_user_page_fault(regs) < 0) {
                return;
            }
        }
        else {
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

            while (1) ;
        }
    }
    check_signal(get_current(), regs);
}

void devicetree_early_init(void *fdt) {
    uart_puts("\n[Kernel] Entering Device Tree Initialization!\n");
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
    // offset = fdt_path_offset(fdt, "/soc/serial");
    // if (offset >= 0) {
    //     prop = fdt_getprop(fdt, offset, "reg", &len);
    //     if (prop) {
    //         const uint64_t *reg = (const uint64_t *)prop;
    //         UART_BASE = bswap64(reg[0]);
    //         UART_LSR_OFFSET = 0x14;
    //     }
    // } else {
    //     offset = fdt_path_offset(fdt, "/soc/uart");
    //     if (offset >= 0) {
    //         prop = fdt_getprop(fdt, offset, "reg", &len);
    //         if (prop) {
    //             const uint64_t *reg = (const uint64_t *)prop;
    //             UART_BASE = bswap64(reg[0]);
    //             UART_LSR_OFFSET = 0x5;
    //         }
    //     }
    // }

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

// =============================== Lab5: Basic Exercise 1 - Thread ===============================


static int nr_threads = 0;

// Linear FIFO Queue
struct task_queue {
    struct task_struct* head;
    struct task_struct* tail;
};

static struct task_queue run_queue = {0, 0};
static struct task_queue zombie_queue = {0, 0};

static void enqueue(struct task_queue* q, struct task_struct* task) {
    task->next = 0; // 確保乾淨的尾巴
    if (q->head == 0) {
        q->head = task;
        q->tail = task;
    } else {
        q->tail->next = task; // 有時候剛開機 在這邊會莫名其妙發出 trap 
        q->tail = task;
    }
}

static struct task_struct* dequeue(struct task_queue* q) {
    if (q->head == 0) {
        return 0; // 空佇列
    }
    struct task_struct* task = q->head;
    q->head = q->head->next;
    
    if (q->head == 0) {
        q->tail = 0; // 拿完如果空了，Tail 也歸零
    }
    return task;
}

void remove_task_from_queue(struct task_queue* q, struct task_struct* target) {
    struct task_struct* curr = q->head;
    struct task_struct* prev = 0;
    while (curr) {
        if (curr == target) {
            if (prev) prev->next = curr->next;
            else q->head = curr->next;
            
            if (q->tail == curr) q->tail = prev;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

struct task_struct* thread_create(void (*threadfn)()) {
    struct task_struct* task = allocate(sizeof(struct task_struct));
    uart_puts("\n[Kernel] Create new task address: ");
    uart_hex((unsigned long)task);
    uart_puts("\n");
    memset(task, 0, sizeof(struct task_struct));
    task->pid = nr_threads++; // 從 0 開始一路往上
    task->stack = (unsigned long)allocate(4096); // order = 0 (4KB) kernel stack
    task->thread.ra = (unsigned long)threadfn; // switch_to (start.S) ret 會來讀 ra 的位址 然後跳到 ra 去執行
    task->thread.sp = task->stack + STACK_SIZE; // kernel stack pointer
    task->state = TASK_RUNNABLE;

    task->user_sp = 0; // user stack pointer = 0, represent kernel thread
    // 一開始建立的 thread 都會是 kernel thread

    if (nr_threads > 1) {
        struct task_struct* curr = get_current();
        if (curr) task->cwd = curr->cwd;
    }
    
    // 初始化 fd_table 為 NULL
    for (int i = 0; i < 16; i++) {
        task->fd_table[i] = NULL;
    }

    enqueue(&run_queue, task); // 加入執行隊列尾端
    return task;
}


extern void switch_to(struct task_struct* prev, struct task_struct* next);

void schedule() {
    struct task_struct* prev = get_current();
    
    // 如果 prev 還活著，就乖乖去隊伍最後面重新排隊
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_RUNNABLE;
        enqueue(&run_queue, prev); 
    }

    // 從隊伍最前面請下一個人上台
    struct task_struct* next = dequeue(&run_queue);
    
    // 如果沒人排隊，就繼續自己跑 (通常是 idle)
    if (next == 0) {
        prev->state = TASK_RUNNING;
        return; 
    }

    next->state = TASK_RUNNING;

    if (prev != next) {
        /* Switch page table before entering the next task. */
        if (next->user_sp != 0 && next->user_pgd) { // user process
            unsigned long next_pgd_pa = (unsigned long)next->user_pgd - PAGE_OFFSET;
            asm volatile(
                "csrw satp, %0\n"
                "sfence.vma zero, zero\n"
                :
                : "r"(MAKE_SATP(next_pgd_pa))
                : "memory"
            );
        } else { // kernel process
            unsigned long kernel_pgd_pa = (unsigned long)pgd - PAGE_OFFSET;
            asm volatile(
                "csrw satp, %0\n"
                "sfence.vma zero, zero\n"
                :
                : "r"(MAKE_SATP(kernel_pgd_pa))
                : "memory"
            );
        }

        // 區分 Kernel Thread 與 User Process
        if (next->user_sp != 0) {
            // 這是一個 User Program (例如 osctest.bin 或 fork 出來的)
            // 它從 U-mode 發生 ecall/中斷時，需要切換到自己的 Kernel Stack
            unsigned long next_kstack_top = next->stack + STACK_SIZE;
            asm volatile("csrw sscratch, %0" : : "r"(next_kstack_top));        
        } 
        else {
            // 這是一個 Kernel Thread (例如 foo, idle, shell)
            // 它發生中斷時「已經」在 Kernel Stack 裡了，所以請 trap.S 不要交換 sp！
            asm volatile("csrw sscratch, 0");
        }
        
        switch_to(prev, next);
    }
}

void thread_exit() {
    struct task_struct* current = get_current();
    current->state = TASK_ZOMBIE;

    enqueue(&zombie_queue, current);

    schedule();
}

struct task_struct* find_task_by_pid(long pid) {
    struct task_struct* curr = run_queue.head;
    while (curr) {
        if (curr->pid == pid) return curr;
        curr = curr->next;
    }

    curr = zombie_queue.head;
    while (curr) {
        if (curr->pid == pid) return curr;
        curr = curr->next;
    }
    
    return NULL; // 沒找到
}

void kill_zombies() {
    struct task_struct* dead_task;
    while ((dead_task = dequeue(&zombie_queue)) != 0) {
        // 清除並關閉此 process 尚未關閉的檔案，避免 memory leak
        for (int i = 0; i < 16; i++) {
            if (dead_task->fd_table[i]) {
                vfs_close(dead_task->fd_table[i]);
                dead_task->fd_table[i] = NULL;
            }
        }
        
        free((void *)dead_task->stack); 
        free(dead_task);
    }
}

void idle() {
    while (1) {
        // kill_zombies();
        schedule();
    }
}

void foo() { // lab 5: Basic Exercise 1 - Thread
    asm volatile("csrsi sstatus, (1 << 1)");
    for (int i = 0; i < 5; i++) {
        uart_puts("Thread ID: ");
        uart_hex(get_current()->pid);
        uart_puts(" ");
        uart_hex(i);
        uart_puts("\n");
        for (int j = 0; j < 100000000; j++); // round robin time
        // schedule();
    }
    thread_exit();
}

// =============================== Lab5: Basic Exercise 2 - User Process and System Call  ===============================

extern void ret_from_exception(); 

long sys_getpid(void);
long sys_uart_read(char *buf, long count);
long sys_uart_write(const char *buf, long count);
int  sys_exec(const char *path);
long sys_fork(struct pt_regs *regs); // fork 需要 Trap Frame 來複製 context
long sys_waitpid(long pid);
void sys_exit(int status);
int  sys_stop(long pid);

/* Kernel shell exec uses spawn+wait semantics via this shared path buffer. */
static char current_exec_path[128];

static void run_user_program(void) {
    if (exec(current_exec_path) < 0) {
        uart_puts("Failed to exec user program.\n");
        thread_exit();
    }
}

// Lab5: Basic Exercise 3 - Video Player
void sys_display(unsigned int *bmp_image, unsigned int width, unsigned int height);
int sys_usleep(unsigned int usec);

// Lab5: Advance Exercise - POSIX Signal
long sys_signal(int signum, void (*handler)());
void sys_sigreturn(struct pt_regs *regs);
int sys_kill(int pid, int signum);
void * sys_mmap(void *addr, unsigned long length, int prot, int flags);

// Lab 7: Basic Exercise 3 - Multitask VFS
int sys_open(const char *pathname, int flags);
int sys_close(int fd);
long sys_read(int fd, void *buf, unsigned long count);
long sys_write(int fd, const void *buf, unsigned long count);
int sys_mkdir(const char *pathname, unsigned mode);
int sys_mount(const char *src, const char *target, const char *filesystem);
int sys_chdir(const char *path);

// Lab 7: Advance Exercise 2 - /dev/fb
long sys_lseek64(int fd, long offset, int whence);
int sys_ioctl(int fd, unsigned long request, unsigned long arg);

static void *mmap_test_addr = NULL;
static unsigned long mmap_test_size = 0;

void syscall_handler(struct pt_regs *regs) {
    // The system call number is stored in a7.
    unsigned long syscall_num = regs->a7;

    switch (syscall_num) {
        case 0: // getpid()
            regs->a0 = sys_getpid();
            break;

        case 1: // uart_read(char *buf, long count)
            // 參數在 a0, a1
            regs->a0 = sys_uart_read((char *)regs->a0, (long)regs->a1);
            break;

        case 2: // uart_write(const char *buf, long count)
            // 參數在 a0, a1
            regs->a0 = sys_uart_write((const char *)regs->a0, (long)regs->a1);
            break;

        case 3: // exec(const char *path)
            regs->a0 = sys_exec((const char *)regs->a0);
            break;

        case 4: // fork()
            // fork 比較特別，需要目前的 regs 狀態來複製 context
            regs->a0 = sys_fork(regs); 
            break;

        case 5: // waitpid(long pid)
            regs->a0 = sys_waitpid((long)regs->a0);
            break;

        case 6: // exit(int status)
            sys_exit((int)regs->a0);
            // exit 通常不會 return
            break;

        case 7: // stop(long pid)
            regs->a0 = sys_stop((long)regs->a0);
            break;

        case 8: // display(unsigned int *bmp_image, unsigned int width, unsigned int height)
            sys_display((unsigned int *)regs->a0, (unsigned int)regs->a1, (unsigned int)regs->a2);
            break;

        case 9: // usleep(unsigned int usec)
            regs->a0 = sys_usleep((unsigned int)regs->a0);
            break;

        case 10: // signal(int signum, void (*handler)())
            regs->a0 = sys_signal((int)regs->a0, (void (*)())regs->a1);
            break;

        case 11: // sigreturn()
            sys_sigreturn(regs); 
            break;

        case 12: // kill(int pid, int signum)
            regs->a0 = sys_kill((int)regs->a0, (int)regs->a1);
            break;

        case 13:
            regs->a0 = (unsigned long)sys_mmap((void *)regs->a0, (unsigned long)regs->a1, (int)regs->a2, (int)regs->a3);
            break;

        case 14: // open(const char *pathname, int flags)
            regs->a0 = sys_open((const char *)regs->a0, (int)regs->a1);
            break;

        case 15: // close(int fd)
            regs->a0 = sys_close((int)regs->a0);
            break;

        case 16: // read(int fd, void *buf, unsigned long count)
            regs->a0 = (long)sys_read((int)regs->a0, (void *)regs->a1, (unsigned long)regs->a2);
            break;

        case 17: // write(int fd, const void *buf, unsigned long count)
            regs->a0 = (long)sys_write((int)regs->a0, (const void *)regs->a1, (unsigned long)regs->a2);
            break;

        case 18: // mkdir(const char *pathname, unsigned mode)
            regs->a0 = sys_mkdir((const char *)regs->a0, (unsigned)regs->a1);
            break;

        case 19: // mount(const char *src, const char *target, const char *filesystem, ...)
            // 根據規格忽略 flags, data
            regs->a0 = sys_mount((const char *)regs->a0, (const char *)regs->a1, (const char *)regs->a2);
            break;

        case 20: // chdir(const char *path)
            regs->a0 = sys_chdir((const char *)regs->a0);
            break;

        case 21: // lseek64(fd, offset, whence)
            regs->a0 = sys_lseek64((int)regs->a0, (long)regs->a1, (int)regs->a2);
            break;

        case 22: // ioctl(fd, request, arg)
            regs->a0 = sys_ioctl((int)regs->a0, (unsigned long)regs->a1, (unsigned long)regs->a2);
            break;


        default:
            uart_puts("Unknown System Call ID: ");
            uart_hex(syscall_num);
            uart_puts("\n");
            regs->a0 = -1; // 傳回失敗
            break;
    }
}

long sys_getpid(void) {
    return get_current()->pid;
}

long sys_uart_read(char *buf, long count) {
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));

    for (int i = 0; i < count; i++) {
        char c = uart_getc();
        asm volatile("csrs sstatus, %0" :: "r"(1UL << 18));
        buf[i] = c;
        asm volatile("csrw sstatus, %0" :: "r"(sstatus));
        if (c == '\n' || c == '\r') {
            return i + 1;
        }
    }

    asm volatile("csrw sstatus, %0" :: "r"(sstatus));
    return count;
}

long sys_uart_write(const char *buf, long count) {
    // 開啟 SUM bit 讓 Kernel Mode 可以讀到 User Mode 的 data
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    for (long i = 0; i < count; i++) {
        asm volatile("csrs sstatus, %0" :: "r"(1UL << 18));
        uart_putc(buf[i]);
        asm volatile("csrw sstatus, %0" :: "r"(sstatus));
    }
    return count;
}

// uncheck
int sys_exec(const char *path) {
    // Return 0 on success, -1 on failure.
    if (!cpio_base) return -1;

    // 開啟 SUM bit
    // 讓 S-mode 的 kernel 可以存取 user space 的記憶體
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 18));

    struct task_struct *curr = get_current();
    struct pt_regs *regs = (struct pt_regs *)(curr->stack + STACK_SIZE - sizeof(struct pt_regs));
    const char *ptr = (const char *)cpio_base;
    while (1) {
        const struct cpio_t *header = (const struct cpio_t *)ptr;
        if (strncmp(header->magic, "070701", 6) != 0) break;

        uint32_t namesize = (uint32_t)hextoi(header->namesize, 8);
        uint32_t filesize = (uint32_t)hextoi(header->filesize, 8);
        const char *cur_filename = ptr + sizeof(struct cpio_t);

        if (!strcmp(cur_filename, "TRAILER!!!")) break;

        uint32_t data_offset = (uint32_t)align(sizeof(struct cpio_t) + namesize, 4);

        if (!strcmp(cur_filename, path)) { // 找到了
            unsigned long target_address = (unsigned long)(ptr + data_offset);

            unsigned long pages = (filesize + 4096UL - 1) / 4096UL;
            if (pages == 0) pages = 1;

            // 用來儲存所有 code 的 pages 的 physical address 的陣列
            unsigned long *pa_list = (unsigned long *)allocate(pages * sizeof(unsigned long));
            if (!pa_list) {
                asm volatile("csrw sstatus, %0" :: "r"(sstatus));
                return -1;
            }

            // 把所有 user program 的 code 放到一個一個的 page 然後再把每一個 page 的 physical address 存到 pa 陣列裡面
            for (unsigned long i = 0; i < pages; i++) {
                struct page *p = alloc_pages(0);
                if (!p) {
                    asm volatile("csrw sstatus, %0" :: "r"(sstatus));
                    return -1;
                }

                unsigned long pa = buddy_memory_base + (unsigned long)(p - mem_map) * 4096UL;
                unsigned long copy_offset = i * 4096UL;
                unsigned long remain = (filesize > copy_offset) ? (filesize - copy_offset) : 0;
                unsigned long copy_len = (remain >= 4096UL) ? 4096UL : remain;

                if (copy_len > 0) {
                    memcpy((void *)(pa + PAGE_OFFSET), (const void *)(target_address + copy_offset), copy_len);
                }
                if (copy_len < 4096UL) {
                    memset((void *)(pa + PAGE_OFFSET + copy_len), 0, 4096UL - copy_len);
                }

                map_pages(curr->user_pgd, i * 4096UL, 4096UL, pa, PAGE_USER_CODE);
                pa_list[i] = pa;
            }

            /* Demand paging: stack is not mapped here; it will be allocated on first fault. */
            // unsigned long stack_pa = (curr->user_sp >= USER_STACK_SIZE) ? (curr->user_sp - USER_STACK_SIZE) : 0;
            // if (stack_pa == 0) {
            //     asm volatile("csrw sstatus, %0" :: "r"(sstatus));
            //     return -1;
            // }
            // map_pages(curr->user_pgd, USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_SIZE, stack_pa, PAGE_USER_STACK); // pgd, va, size, pa, proto

            curr->user_sp = USER_STACK_TOP;
            curr->user_code_phys_pages = pa_list;
            curr->user_code_pages = pages;

            /* create VMA entries for code */
            struct vma *code_vma = (struct vma *)allocate(sizeof(struct vma));
            if (code_vma) {
                code_vma->start = 0UL;
                code_vma->len = pages * 4096UL;
                code_vma->prot = PROT_READ | PROT_EXEC;
                code_vma->flags = 0;
                code_vma->next = NULL;
                vma_insert_sorted(&curr->vm_list, code_vma);
            }

            /* create VMA entries for stack */
            unsigned long stack_va = USER_STACK_TOP - USER_STACK_SIZE;
            struct vma *stack_vma = (struct vma *)allocate(sizeof(struct vma));
            if (stack_vma) {
                stack_vma->start = stack_va;
                stack_vma->len = USER_STACK_SIZE;
                stack_vma->prot = PROT_READ | PROT_WRITE;
                stack_vma->flags = 0;
                stack_vma->next = NULL;
                vma_insert_sorted(&curr->vm_list, stack_vma);
            }

            /* Switch to the new user page table before returning from trap. */
            unsigned long pgd_pa = (unsigned long)curr->user_pgd - PAGE_OFFSET;
            asm volatile(
                "csrw satp, %0\n"
                "sfence.vma zero, zero\n"
                :
                : "r"(MAKE_SATP(pgd_pa))
                : "memory");

            /* Return path will execute sret with these values. */
            regs->sepc = 0x0UL;
            regs->sp = curr->user_sp;
            regs->a0 = 0;
            regs->sstatus &= ~(1UL << 8); // SPP = 0, back to U-mode
            regs->sstatus |= (1UL << 5);  // SPIE = 1

            asm volatile("csrw sstatus, %0" :: "r"(sstatus));
            return 0;
        }
        ptr += align(data_offset + filesize, 4);
    }
    uart_puts("sys_exec: file not found: ");
    uart_puts(path);
    uart_puts("\n");
    asm volatile("csrw sstatus, %0" :: "r"(sstatus));
    return -1;
}

long sys_fork(struct pt_regs *regs) {
    struct task_struct* parent = get_current();
    unsigned long sstatus;
    int has_user_space = (parent && parent->user_sp != 0 && parent->user_pgd);

    struct task_struct* child = allocate(sizeof(struct task_struct));
    if (!child) return -1;
    memset(child, 0, sizeof(struct task_struct));
    child->pid = nr_threads++;
    child->stack = (unsigned long)allocate(4096); // 核心堆疊 (Kernel Stack)
    if (!child->stack) {
        free(child);
        return -1;
    }
    child -> state = TASK_RUNNABLE;
    child -> vm_list = NULL;
    child -> user_pgd = NULL;
    child -> cwd = parent -> cwd;

    // 初始化 fd_table 為 NULL 
    for (int i = 0; i < 16; i++) {
        child -> fd_table[i] = parent -> fd_table[i];
        if (child -> fd_table[i]) {
            child -> fd_table[i]->refcount++; // 複製 parent fd_table 給 child 時，增加 file 的 refcount
        }
    }

    // copy parent thread's kernel stack
    for (int i = 0; i < 4096; i++) {
        ((char*)child -> stack)[i] = ((char*)parent -> stack)[i];
    }

    /* VM-aware fork: clone user address space into child's own user_pgd. */
    // 替 child 建立一份獨立的 user address space，並把 parent 的 user memory 複製過去。
    if (has_user_space) {
        // 打開 SUM bit 讓 kernel space 可以讀寫 user space 記憶體
        asm volatile("csrr %0, sstatus" : "=r"(sstatus));
        asm volatile("csrs sstatus, %0" :: "r"(1UL << 18));

        child -> user_pgd = alloc_user_pgd();
        if (!child -> user_pgd) {
            asm volatile("csrw sstatus, %0" :: "r"(sstatus));
            free((void *)child -> stack);
            free(child);
            return -1;
        }
        child -> user_code_src = parent -> user_code_src;
        child -> user_code_size = parent -> user_code_size;
        child -> user_code_phys_pages = NULL;
        child -> user_code_pages = 0;
        child -> user_stack_phys_base = 0;

        // 把 parent 的 VMA linked list 複製一份給 child。
        // 不會複製實際的頁面內容。實際內容是後面 fork 迴圈裡再一頁一頁複製的。
        // 讓 child 的 vma list 長得跟 parent vma list 一樣
        child -> vm_list = vma_clone_list(parent -> vm_list);
        if (parent -> vm_list && !child -> vm_list) {
            asm volatile("csrw sstatus, %0" :: "r"(sstatus));
            free_task_user_space(child);
            free((void *)child -> stack);
            free(child);
            return -1;
        }

        child -> user_sp = regs -> sp;

        // CoW fork: share parent pages with child and make writable mappings read-only.
        // 把 parent 的每一個 VMA 都走一遍
        for (struct vma *v = parent -> vm_list; v; v = v -> next) {
            unsigned long va;
            unsigned long start = v -> start & ~(4096UL - 1); // aligned to 4KB
            unsigned long end = (v -> start + v -> len + 4096UL - 1) & ~(4096UL - 1);
            // 把 VMA 內每一個 4KB page 都走一遍
            // 先讓 parent 和 child 共享同一塊 physical page，之後有人要寫才真的 copy。
            for (va = start; va < end; va += 4096UL) {
                unsigned long *src_pte = user_pte_entry(parent -> user_pgd, va);
                unsigned long pa;
                struct page *page;
                unsigned long shared_flags;

                if (!src_pte || !(*src_pte & PTE_V)) continue;

                pa = (((unsigned long)(*src_pte)) >> 10) << 12;
                page = mem_map + ((pa - buddy_memory_base) / 4096UL);
                if (page->refcount == 0) {
                    asm volatile("csrw sstatus, %0" :: "r"(sstatus));
                    free_task_user_space(child);
                    free((void *)child->stack);
                    free(child);
                    return -1;
                }

                page->refcount++;
                shared_flags = *src_pte;
                if (shared_flags & PTE_W) {
                    shared_flags &= ~PTE_W;
                    *src_pte = shared_flags;
                }
                map_pages(child->user_pgd, va, 4096UL, pa, shared_flags);
            }
        }

        asm volatile("sfence.vma zero, zero\n" ::: "memory");
        asm volatile("csrw sstatus, %0" :: "r"(sstatus));

    } else {
        child->user_sp = 0;
        child->user_pgd = 0;
        child->user_code_src = 0;
        child->user_code_size = 0;
        child->user_code_phys_pages = 0;
        child->user_code_pages = 0;
        child->user_stack_phys_base = 0;
    }

    // Note that only processes forked after signal is called will inherit the handler.
    /* Copy signal handlers from parent (inherit signal registrations) */
    // 把 signal handler 複製過去
    for (int i = 0; i < 32; i++) {
        child->handlers[i] = parent->handlers[i];
    }
    child->pending_signals = 0;
    child->is_handling_signal = 0;
    child->signal_stack_base = 0;

    // 4. 計算 Trap Frame 位址並設定醒來路徑
    // 是目前（父程序）被 trap/ syscall 時的暫存器快照（trapframe），包含所有通用暫存器、sepc、sstatus 等，用來記錄要回到使用者態時要恢復的 CPU 狀態。
    struct pt_regs* child_regs = (struct pt_regs*)(child->stack + STACK_SIZE - sizeof(struct pt_regs));

    *child_regs = *regs; // 複製所有暫存器狀態

    // 5. 設定回傳值與更新子行程的狀態
    child_regs->a0 = 0;              // Return the child’s pid to the parent, and 0 to the child.
    child_regs->sp = child->user_sp; // user thread uses virtual SP under child->user_pgd

    child_regs->tp = (unsigned long)child; 

    child->thread.ra = (unsigned long)ret_from_exception;
    child->thread.sp = (unsigned long)child_regs;

    enqueue(&run_queue, child);

    return (long)child->pid; // Return the child’s pid to the parent, and 0 to the child. (child's return value in a0)
}

long sys_waitpid(long pid) { // 不確定會不會跟 kill_zombies() 有問題？
    while (1) {
        struct task_struct* target = find_task_by_pid(pid);
        if (target == NULL) return pid; 

        if (target->state == TASK_ZOMBIE) {
            remove_task_from_queue(&zombie_queue, target);

            if (target->stack) {
                free((void *)target->stack);
            }
            
            // 清除並關閉此 process 尚未關閉的檔案，避免 memory leak
            for (int i = 0; i < 16; i++) {
                if (target->fd_table[i]) {
                    vfs_close(target->fd_table[i]);
                    target->fd_table[i] = NULL;
                }
            }

            free_task_user_space(target);
            free(target);
            
            return pid;
        }

        schedule();
    }
}

void sys_exit(int status) {
    struct task_struct* curr = get_current();

    curr->state = TASK_ZOMBIE;
    enqueue(&zombie_queue, curr);

    schedule();
}

int sys_stop(long pid) {
    struct task_struct* target = find_task_by_pid(pid);
    if (target == NULL || target->state == TASK_ZOMBIE) return -1; 

    target->state = TASK_ZOMBIE;
    
    remove_task_from_queue(&run_queue, target);
    enqueue(&zombie_queue, target);
    
    return 0;
}

// =============================== Lab5: Basic Exercise 3 - Video Player  ===============================

#define FB_BASE   0x7f700000
#define FB_WIDTH  1920
#define FB_HEIGHT 1080
#define CACHE_BLOCK_SIZE 64

#define cbo_flush(start)                \
    ({                                  \
        asm volatile("mv a0, %0\n\t"    \
                     ".word 0x0025200F" \
                     :                  \
                     : "r"(start)       \
                     : "memory", "a0"); \
    })

static void flush_dcache(void* addr, unsigned long len) {
    unsigned long start = (unsigned long)addr & ~(CACHE_BLOCK_SIZE - 1);
    __sync_synchronize(); // 確保前面的 memory 操作都完成了
    for (unsigned long line = start; line < (unsigned long)addr + len; line += CACHE_BLOCK_SIZE) {
        cbo_flush(line);
        __sync_synchronize();
    }
}

void sys_display(unsigned int *bmp_image, unsigned int width, unsigned int height) {
    // 開啟 SUM bit
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 18));

    unsigned int *fb = (unsigned int *)(FB_BASE + PAGE_OFFSET);
    
    int start_x = (FB_WIDTH - width) / 2;
    int start_y = (FB_HEIGHT - height) / 2;

    // 逐行將影片像素搬運到 Framebuffer
    for (int y = 0; y < height; y++) {
        void *dst = fb + (start_y + y) * FB_WIDTH + start_x;
        
        memcpy(dst, bmp_image + y * width, width * sizeof(unsigned int));
        
        flush_dcache(dst, width * sizeof(unsigned int));
    }

    // 還原 sstatus
    asm volatile("csrw sstatus, %0" :: "r"(sstatus));
}

int sys_usleep(unsigned int usec) {
    unsigned long start = rdtime();
    unsigned long delta_ticks = (unsigned long)usec * (ticks_per_sec / 1000000);
    unsigned long end = start + delta_ticks;

    while (rdtime() < end) {
        schedule(); 
    }

    return 0;
}

// =============================== Lab5: Advance Exercise - POSIX Signal  ===============================

void check_signal(struct task_struct *task, struct pt_regs *regs) {
    if (!task) return;

    if (task->user_sp == 0) return; // only user threads

    if (task->pending_signals == 0 || task->is_handling_signal) return;

    int signum = -1;
    for (int i = 0; i < 32; i++) {
        if (task->pending_signals & (1UL << i)) {
            signum = i;
            break;
        }
    }
    if (signum < 0) return;

    uart_puts("[check_signal] PID ");
    uart_hex(task->pid);
    uart_puts(" signal ");
    uart_hex(signum);
    uart_puts(" handler=0x");
    uart_hex((unsigned long)task->handlers[signum]);
    uart_puts("\n");

    if (task->handlers[signum] == NULL) {
        uart_puts("No handler for signal, terminate process.\n");
        
        task->pending_signals &= ~(1UL << signum); // 移除標籤
        
        if (task == get_current()) sys_exit(0);
        return;
    }

    /* save user context and prepare signal stack */
    task->saved_context = *regs;

    struct page *sig_page = alloc_pages(0);
    if (!sig_page) return; // cannot deliver
    task->signal_stack_base = (void *)sig_page;

    unsigned long sig_base = buddy_memory_base + (unsigned long)(sig_page - mem_map) * 4096UL;
    unsigned long sig_stack_top = sig_base + 4096UL;

    /* write trampoline at the end of signal stack: addi a7, x0, 11; ecall */
    unsigned int *tramp = (unsigned int *)(sig_stack_top - 8);
    tramp[0] = 0x00B00893U; /* addi a7, x0, 11 (System Call Number) sigreturn */
    tramp[1] = 0x00000073U; /* ecall */

    /* place trampoline address on stack so handler's ret pops it as return address */
    unsigned long *stack_return_addr = (unsigned long *)(sig_stack_top - 16);
    *stack_return_addr = (unsigned long)(sig_stack_top - 8);

    /* redirect execution to handler in user mode */
    regs->sepc = (unsigned long)task->handlers[signum];
    regs->sp = sig_stack_top - 16;  /* sp points to return address on stack */
    regs->ra = (unsigned long)(sig_stack_top - 8);  /* ra also points to trampoline for safety */

    uart_puts("[check_signal] handler sepc=0x");
    uart_hex(regs->sepc);
    uart_puts(" sp=0x");
    uart_hex(regs->sp);
    uart_puts(" ra=0x");
    uart_hex(regs->ra);
    uart_puts("\n");

    task->is_handling_signal = 1;
    task->pending_signals &= ~(1UL << signum);
}

long sys_signal(int signum, void (*handler)()) {
    // system call 是 user program 求 Kernel 辦事
    // signal 是 kernel 逼 user program 辦事
    // 例如：平常在 linux ctrl + c 這是 SIGINT 會終止程式
    // sys_signal() 本質上是讓 user program 去定義 哪一個 signal number 對應到哪一個 handler

    if (signum < 0 || signum >= 32) return -1;
    struct task_struct *curr = get_current();
    curr->handlers[signum] = handler;
    uart_puts("[sys_signal] PID ");
    uart_hex(curr->pid);
    uart_puts(" signal ");
    uart_hex(signum);
    uart_puts(" handler=0x");
    uart_hex((unsigned long)handler);
    uart_puts("\n");
    return 0;
}

void sys_sigreturn(struct pt_regs *regs) {
    // You must print a message in the sigreturn function to verify your implementation is correct. 
    // The message should be printed every time a signal handler finishes.

    struct task_struct *curr = get_current();
    
    uart_puts("sigreturn: restoring context for PID ");
    uart_hex(curr->pid);
    uart_puts("\n");

    *regs = curr->saved_context;

    // recycle signal handler's stack
    if (curr->signal_stack_base) {
        free_pages((struct page*)curr->signal_stack_base);
        curr->signal_stack_base = 0;
    }

    curr->is_handling_signal = 0;
}

// SIGTERM
int sys_kill(int pid, int signum) {
    if (signum < 0 || signum >= 32) return -1;

    struct task_struct *target = find_task_by_pid(pid);
    if (!target) return -1;

    uart_puts("[sys_kill] send signal ");
    uart_hex(signum);
    uart_puts(" to PID ");
    uart_hex(pid);
    uart_puts("\n");

    target->pending_signals |= (1UL << signum);

    if (target->state == TASK_ZOMBIE) return -1;

    return 0;
}

// =============================== Lab6: Advanced Exercise 1 - Mmap  ===============================

// return generic pointer, 就是不知道是什麼資料型態的 pointer
// 在 addr 為起點 開一個 length 長度的 prot 的 虛擬記憶體區塊，flags 標記他是 anonymous or populate
// return user space 的 va
void * sys_mmap(void *addr, unsigned long length, int prot, int flags) {
    struct task_struct *curr = get_current();
    if (!curr || !curr->user_pgd || curr->user_sp == 0) return (void *)-1;

    const unsigned long MAP_ANONYMOUS = 0x20UL;
    const unsigned long MAP_POPULATE = 0x8000UL;
    const unsigned long PAGE = 4096UL;
    const unsigned long UMAX = ~0UL;

    if (length == 0) return (void *)-1;
    // Because this lab does not use ELF files or actual files on a file system, you only need to implement anonymous page mapping.
    // Lab6 不會有不是 MAP_ANONYMOUS 的情況
    if (!(flags & MAP_ANONYMOUS)) return (void *)-1; 

    /* align length up to page */
    unsigned long llen = (length + PAGE - 1) & ~(PAGE - 1); // eg. length = 1 -> 4096 / length = 4095 -> 4096

    unsigned long hint = (unsigned long)addr;
    int exact = 0;
    if (addr) {
        if (hint & (PAGE - 1)) {
            exact = 0; /* not aligned -> treat as hint */
        } else {
            /* if no overlap with existing VMAs, use exact */
            if (hint <= UMAX - llen) { // 避免 hint + llen 會溢位
                unsigned long end = hint + llen;
                if (!vma_find_overlap(curr->vm_list, hint, end)) exact = 1;
            }
        }
    }

    // Current Process 的 VMA list中，根據長度 llen、位址提示 hint 與是否要求 exact，
    // 找出一段可用的起始虛擬位址並存到 base
    // 目前 curr->vm_list 裡沒有被任何 VMA 佔用的一段區間
    unsigned long base = vma_find_free_range(curr->vm_list, llen, hint, exact);
    if (base == 0) return (void *)-1;

    // 暫時關閉 s-mode interrupt
    // 進入 critical section
    // 若在中間被 interrupt（或被搶占切換掉）：可能會留下半完成的 vm_list（別的處理路徑或 ISR 看到會出錯）。
    unsigned long flags_save = local_irq_save();

    /* create VMA metadata */
    struct vma *v = (struct vma *)allocate(sizeof(struct vma));
    if (!v) {
        local_irq_restore(flags_save);
        return (void *)-1;
    }
    v->start = base;
    v->len = llen;
    v->prot = prot;
    v->flags = flags;
    v->next = NULL;
    vma_insert_sorted(&curr->vm_list, v);

    /* If MAP_POPULATE, allocate and map pages eagerly */
    // MAP_POPULATE 代表在 mmap 當下就先把 page 準備好，不是等到 page fault 才處理
    if (flags & MAP_POPULATE) {
        unsigned long pte_flags = prot_to_pte_flags(prot);
        unsigned long npages = llen / PAGE;
        unsigned long *alloced_pa = (unsigned long *)allocate(npages * sizeof(unsigned long));
        if (!alloced_pa) {
            vma_remove(&curr->vm_list, base, llen);
            local_irq_restore(flags_save);
            return (void *)-1;
        }

        unsigned long i;
        for (i = 0; i < npages; i++) {
            struct page *pg = alloc_pages(0);
            if (!pg) break;
            unsigned long pa = buddy_memory_base + (unsigned long)(pg - mem_map) * PAGE;
            memset((void *)(pa + PAGE_OFFSET), 0, PAGE);
            alloced_pa[i] = pa;
            map_pages(curr->user_pgd, base + i * PAGE, PAGE, pa, pte_flags);
        }

        if (i != npages) {
            /* rollback: unmap mapped pages and free allocated pages */
            if (i > 0) unmap_pages(curr->user_pgd, base, i * PAGE);
            for (unsigned long j = 0; j < i; j++) {
                unsigned long pa = alloced_pa[j];
                if (pa) {
                    struct page *p = mem_map + ((pa - buddy_memory_base) / PAGE);
                    free_pages(p);
                }
            }
            free(alloced_pa);
            vma_remove(&curr->vm_list, base, llen);
            local_irq_restore(flags_save);
            return (void *)-1;
        }
        free(alloced_pa);
    }

    asm volatile("sfence.vma zero, zero\n" ::: "memory");

    local_irq_restore(flags_save);
    return (void *)base;
}

void mmap_w() {
    struct task_struct *curr = get_current();
    if (!curr) {
        uart_puts("mmap_w: no current task\n");
        return;
    }

    const unsigned long MAP_ANONYMOUS = 0x20UL;
    const unsigned long MAP_POPULATE = 0x8000UL;
    const unsigned long SIZE = 8192UL; /* 2 pages */

    void *p = sys_mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE);
    if (p == (void *)-1) {
        uart_puts("mmap_w: mmap failed\n");
        return;
    }

    mmap_test_addr = p;
    mmap_test_size = SIZE;

    uart_puts("mmap_w: mmap returned "); uart_hex((unsigned long)p); uart_puts("\n");

    /* switch to this task's page table to access user VA directly */
    unsigned long old_satp;
    asm volatile("csrr %0, satp" : "=r"(old_satp)); // 先把當前 satp 讀出來存在 old_stap
    if (curr->user_pgd) {
        // 在 kernel mode 裡，把目前使用的頁表從 kernel page table 暫時切到這個 process 的 user page table
        unsigned long pgd_pa = (unsigned long)curr->user_pgd - PAGE_OFFSET;
        unsigned long new_satp = MAKE_SATP(pgd_pa);
        asm volatile("csrw satp, %0\n" :: "r"(new_satp) : "memory");
        asm volatile("sfence.vma zero, zero\n" ::: "memory");

        /* write pattern */
        volatile unsigned char *kptr = (volatile unsigned char *)((unsigned long)p + PAGE_OFFSET);
        for (unsigned long i = 0; i < SIZE; i++) kptr[i] = (unsigned char)(i & 0xFF);

        /* restore */
        asm volatile("csrw satp, %0\n" :: "r"(old_satp) : "memory");
        asm volatile("sfence.vma zero, zero\n" ::: "memory");

        uart_puts("mmap_w: write finished\n");
    } else {
        uart_puts("mmap_w: no user pgd for current task\n");
    }
}

void mmap_r() {
    struct task_struct *curr = get_current();
    if (!curr) {
        uart_puts("mmap_r: no current task\n");
        return;
    }

    if (!mmap_test_addr || mmap_test_size == 0) {
        uart_puts("mmap_r: no saved mapping\n");
        return;
    }

    uart_puts("mmap_r: checking saved mapping "); uart_hex((unsigned long)mmap_test_addr); uart_puts("\n");

    unsigned long old_satp;
    asm volatile("csrr %0, satp" : "=r"(old_satp));
    if (curr->user_pgd) {
        unsigned long pgd_pa = (unsigned long)curr->user_pgd - PAGE_OFFSET;
        unsigned long new_satp = MAKE_SATP(pgd_pa);
        asm volatile("csrw satp, %0\n" :: "r"(new_satp) : "memory");
        asm volatile("sfence.vma zero, zero\n" ::: "memory");

        volatile unsigned char *kptr = (volatile unsigned char *)((unsigned long)mmap_test_addr + PAGE_OFFSET);
        unsigned long errors = 0;
        for (unsigned long i = 0; i < mmap_test_size; i++) {
            unsigned char v = kptr[i];
            if (v != (unsigned char)(i & 0xFF)) errors++;
        }

        asm volatile("csrw satp, %0\n" :: "r"(old_satp) : "memory");
        asm volatile("sfence.vma zero, zero\n" ::: "memory");

        if (errors == 0) uart_puts("mmap_r: read OK\n");
        else {
            uart_puts("mmap_r: read mismatch, errors="); uart_hex(errors); uart_puts("\n");
        }
    } else {
        uart_puts("mmap_r: no user pgd for current task\n");
    }
}

// =============================== Lab7: Basic Exercise 3 - Multitask VFS  ===============================

#define O_CREAT 00000100

int get_unused_fd(struct task_struct* task) {
    for (int i = 0; i < 16; i++) {
        if (task -> fd_table[i] == NULL) return i;
    }
    return -1; // Process FD table full
}

int sys_open(const char *pathname, int flags) {
    // pathname = 0x0000000000001650

    // --- 在複製的瞬間開啟 SUM ---
    // 先把 pathname 從 user space copy 一份到 kernel space
    char k_path[128]; // Kernel Buffer
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 18));
    strcpy(k_path, pathname);
    asm volatile("csrw sstatus, %0" :: "r"(sstatus));
    // 關閉 SUM bit
    // ----------------------------

    if (pathname == NULL)   return -1;
    
    struct task_struct *curr = get_current();

    // 在 process file descriptor table 中 找空位
    int fd = get_unused_fd(curr);
    if (fd < 0) return -1; // FD Table 已滿

    // 呼叫 VFS 層開啟檔案
    struct file *f = NULL;
    int ret = vfs_open(k_path, flags, &f);
    
    if (ret != 0)   return ret; // 開啟失敗 (包含找不到檔案且沒設定 O_CREAT)

    curr -> fd_table[fd] = f;

    return fd;
}

int sys_close(int fd) {
    struct task_struct *curr = get_current();

    if (fd < 0 || fd >= 16) return -1; // 錯誤：無效的 FD
    if (curr -> fd_table[fd] == NULL) return -1; // 錯誤：無效的 FD

    int ret = vfs_close(curr -> fd_table[fd]);
    if (ret != 0)   return ret; // 若底層關閉失敗，回傳錯誤

    curr -> fd_table[fd] = NULL;

    return 0; // 成功
}

long sys_read(int fd, void *buf, unsigned long count) {
    if (buf == NULL)    return -1;
    // 去讀 fd 的這個檔案，把他讀到 buf 這個 buffer，然後讀 count 這麼多個資料量
    struct task_struct *curr = get_current();
    // 1. 邊界檢查與合法性驗證
    if (fd < 0 || fd >= 16 || curr -> fd_table[fd] == NULL)   return -1; // 無效的 FD 或未開啟
    struct file *file = curr -> fd_table[fd];

    char k_buf[128];
    unsigned long read_count = count > sizeof(k_buf) ? sizeof(k_buf) : count;

    // 2. 讓 VFS 把資料讀進絕對安全的 Kernel Buffer (完全不需要開 SUM)
    // 這時候就算 uart_getc 卡住、狂切換進程，也絕對不會當機！
    long read_bytes = vfs_read(file, k_buf, read_count);

    if (read_bytes > 0) {
        // 3. 讀取完畢後，在最安全的時機開啟 SUM，一口氣把資料倒回 User Space
        unsigned long sstatus;
        asm volatile("csrr %0, sstatus" : "=r"(sstatus));
        asm volatile("csrs sstatus, %0" :: "r"(1UL << 18)); // 開啟 SUM

        // 🎯 如果 buf 剛好是 Fork 後的唯讀 CoW 分頁，這裡會瞬間觸發 Page Fault，
        // 並且完美流暢地執行完你寫的 handle_user_page_fault，解鎖後繼續完成 memcpy！
        memcpy(buf, k_buf, read_bytes);

        asm volatile("csrw sstatus, %0" :: "r"(sstatus)); // 關閉 SUM
    }
    return read_bytes;
}

long sys_write(int fd, const void *buf, unsigned long count) {
    if (buf == NULL)    return -1;

    struct task_struct *curr = get_current();
    if (fd < 0 || fd >= 16 || curr->fd_table[fd] == NULL)   return -1; // 無效的 FD 或未開啟

    struct file *file = curr -> fd_table[fd];

    // 2. 檢查寫入權限 (選擇性：如果你的 FS 支援 Read-only 標記)
    // 假設你在 file->flags 或 vnode 屬性中有標記是否唯讀
    // if (is_read_only(file)) return -1;

    char k_buf[128];
    unsigned long write_count = count > sizeof(k_buf) ? sizeof(k_buf) : count;

    // --- 關鍵修改：開啟 SUM 位元 ---
    // 因為 buf 是 user space 的 pointer
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 18)); // 設定 SUM (bit 18)
    memcpy(k_buf, buf, write_count);

    asm volatile("csrw sstatus, %0" :: "r"(sstatus)); // 關閉 SUM

    // 2. 讓底層驅動慢慢去寫 (此時不需要 SUM，不怕任何記憶體衝突)
    long written_bytes = vfs_write(file, k_buf, write_count);

    return written_bytes; // Returns the number of bytes successfully written on success
}

int sys_mkdir(const char *pathname, unsigned mode) {
    if (pathname == NULL) return -1;
    // 把 pathname 從 user space copy 到 kernel space
    char k_path[128];
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 18));
    strcpy(k_path, pathname);
    asm volatile("csrw sstatus, %0" :: "r"(sstatus));
    // ---------------------------------------------
    int ret = vfs_mkdir(k_path);
    return ret;
}

int sys_mount(const char *src, const char *target, const char *filesystem) {
    if (target == NULL || filesystem == NULL) return -1;
    // 把 target 跟 filesystem 從 user space copy 到 kernel space
    char k_target[128];
    char k_filesystem[128];
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 18));
    strcpy(k_target, target);
    strcpy(k_filesystem, filesystem);
    asm volatile("csrw sstatus, %0" :: "r"(sstatus));
    // ---------------------------------------------------------
    int ret = vfs_mount(k_target, k_filesystem);
    return ret;
}

int sys_chdir(const char *path) { // change current working directory
    // 把 pathname 從 user space copy 到 kernel space
    char k_path[128];
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 18));
    strcpy(k_path, path);
    asm volatile("csrw sstatus, %0" :: "r"(sstatus));
    // ---------------------------------------------
    
    struct task_struct *curr = get_current();
    struct vnode *target = NULL;

    // 1. 路徑解析
    // 這裡必須呼叫支援從 CWD 開始搜尋的 vfs_lookup_at (或修改後的 vfs_lookup)
    // 如果 path 是絕對路徑，vfs_lookup 應該從根目錄開始
    // 如果是相對路徑，則從 curr->cwd 開始搜尋
    if (vfs_lookup(k_path, &target) != 0) return -1; // 路徑找不到

    // 2. 驗證目標是否為目錄
    // 透過 internal 指標轉型取得該節點的 inode 屬性
    // 注意：你必須確保每個檔案系統都有一個檢查是否為目錄的方法
    // 這裡假設 tmpfs_inode 有 is_dir 標記
    struct tmpfs_inode* inode = (struct tmpfs_inode*)target -> internal;
    if (!inode -> is_dir) return -1; // 目標不是目錄，無法切換

    // 3. 更新 Task 的 CWD
    curr -> cwd = target;
    return 0;
}

// =============================== Lab7: Advance Exercise 2 - /dev/fb  ===============================

long sys_lseek64(int fd, long offset, int whence) {
    struct task_struct *curr = get_current();
    if (fd < 0 || fd >= 16 || curr->fd_table[fd] == NULL) return -1;
    
    return vfs_lseek64(curr -> fd_table[fd], offset, whence);
}

// 2. 實作 sys_ioctl
int sys_ioctl(int fd, unsigned long request, unsigned long arg) {
    struct task_struct *curr = get_current();
    if (fd < 0 || fd >= 16 || curr->fd_table[fd] == NULL) return -1;
    
    return vfs_ioctl(curr -> fd_table[fd], request, arg);
}

// =============================== Lab6: Basic Exercise 1 - Virtual Memory in Kernel Space  ===============================

#define NUM_PAGES 0x280000

/* Memory map */
#define PAGE_OFFSET 0xffffffc000000000UL
#define PAGE_SIZE_4KB   (1UL << 12)   // 4KB
#define PMD_SIZE_2MB      (1UL << 21) // 2MB
#define PGD_SIZE_1GB      (1UL << 30) // 1GB
#define PFN_DOWN(x) ((x) >> 12) // 實體位址除以 4096 = Physical Page Number

#define PHYS_RAM_BASE 0x00000000UL  // 0x0000_0000_0000_0000 - 0x0000_0000_7FFF_FFFF (2 GiB)
#define PHYS_RAM_SIZE 0x40000000UL  // 1GB

#define UART_PHYS_BASE   0xD4017000UL 
#define UART_VA (UART_PHYS_BASE + PAGE_OFFSET)

/* VA bit-field shifts (Sv39) */
#define PGD_SHIFT     30 
#define PMD_SHIFT     21
#define PTE_SHIFT     12

#define ENTRIES_PER_TABLE  512

#define IDENTITY_PGD_INDEX (((PHYS_RAM_BASE) >> PGD_SHIFT) & 0x1FF)
#define KERNEL_PGD_INDEX   ((PAGE_OFFSET >> PGD_SHIFT) & 0x1FF)
#define UART_PGD_INDEX     ((UART_VA >> PGD_SHIFT) & 0x1FF)
#define UART_PMD_INDEX     ((UART_VA >> PMD_SHIFT) & 0x1FF)
#define UART_PTE_INDEX     ((UART_VA >> PTE_SHIFT) & 0x1FF)

#define LINEAR_MAP_GIB     4

/* Page protection bits */
#define PTE_V  (1 << 0) // PAGE_PRESENT
#define PTE_R  (1 << 1) // PAGE_READ
#define PTE_W  (1 << 2) // PAGE_WRITE
#define PTE_X  (1 << 3) // PAGE_EXEC
#define PTE_U  (1 << 4) // PAGE_USER
#define PTE_G  (1 << 5) // PAGE_GLOBAL
#define PTE_A  (1 << 6) // PAGE_ACCESSED
#define PTE_D  (1 << 7) // PAGE_DIRTY

#define PROT_KERNEL     (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define PAGE_MMIO       (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)
#define PAGE_USER_CODE  (PTE_V | PTE_R | PTE_X | PTE_U | PTE_A | PTE_D)  
#define PAGE_USER_STACK (PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D)        

#define USER_STACK_TOP 0x0000004000000000UL

// SV39 (Its top 4 bits [63:60] select the translation mode (8 = Sv39) (0 if turn off MMU))
#define SATP_SV39 (8UL << 60) 
// The low 44 bits [43:0] hold the physical page number (PPN) of the root page table.
#define MAKE_SATP(pgd_pa)   (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))

#define MAKE_PTE(pa, flags) ((((unsigned long)(pa)) >> 12) << 10 | (flags))

// PGD - Unsigned long (8 bytes) * 512 = 4096 bytes = 4KB
unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE_4KB)))  
    pgd[ENTRIES_PER_TABLE] = { 0 };

// PMD - Unsigned long (8 bytes) * 512 = 4096 bytes = 4KB
// 一個 PMD 表有 512 個 entries, 每一個 entries 代表 2MB, 所以一個 PMD 可以代表 512 * 2MB = 1GB 的 Memory
static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE_4KB)))  
    pmd[LINEAR_MAP_GIB][ENTRIES_PER_TABLE] = { { 0 } };

// MMIO PMD
static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE_4KB)))  
    mmio_pmd[ENTRIES_PER_TABLE] = { 0 };
    
// MMIO PTE
static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE_4KB)))  
    mmio_pte[ENTRIES_PER_TABLE] = { 0 };

// Physical Address -> Physical Page Number (Map to 10~53 bits)
#define PA_TO_PTE_PPN(pa) ((((unsigned long)(pa) >> 12) & 0xFFFFFFFFFFF) << 10)

// set_vm() 會在 mm_init() 之前執行，所以連 buddy system 都還沒有
void setup_vm() {
    // 1. Init PGD / PMD / MMIO_PMD / MMIO_PTE 
    for (size_t i = 0; i < ENTRIES_PER_TABLE; i ++) {
        pgd[i] = 0;
        mmio_pmd[i] = 0;
        mmio_pte[i] = 0;
    }

    for (int g = 0; g < LINEAR_MAP_GIB; g++)
        for (int i = 0; i < ENTRIES_PER_TABLE; i++)
            pmd[g][i] = 0;

    // 2. Identity Mapping + Higher-Half - Link PGD to PMD 
    // Before enabling the MMU, set up the kernel page tables using 2 MB pages at the PMD level. 
    // Setup PMD tables
    unsigned long current_pa = PHYS_RAM_BASE; 
    // pmd[0] (0 ~ 1GB) 0x00000000 ~ 0x3FFFFFFF
    for (int i = 0; i < ENTRIES_PER_TABLE; i ++) {
        pmd[0][i] = PA_TO_PTE_PPN(current_pa) | PROT_KERNEL;
        current_pa += PMD_SIZE_2MB;
    }
    // pmd[1] (1 ~ 2GB) 0x40000000 ~ 0x7FFFFFFF
    for (int i = 0; i < ENTRIES_PER_TABLE; i ++) {
        pmd[1][i] = PA_TO_PTE_PPN(current_pa) | PROT_KERNEL;
        current_pa += PMD_SIZE_2MB;
    }
    pgd[IDENTITY_PGD_INDEX] = MAKE_PTE(pmd[0], PTE_V); // Identity Mapping
    pgd[IDENTITY_PGD_INDEX + 1] = MAKE_PTE(pmd[1], PTE_V);
    pgd[KERNEL_PGD_INDEX]   = MAKE_PTE(pmd[0], PTE_V); // Higher-Half
    pgd[KERNEL_PGD_INDEX + 1]   = MAKE_PTE(pmd[1], PTE_V);
    
    // 3. MMIO Mapping 
    pgd[UART_PGD_INDEX]     = MAKE_PTE(mmio_pmd, PTE_V);
    mmio_pmd[UART_PMD_INDEX]= MAKE_PTE(mmio_pte, PTE_V);
    
    mmio_pte[UART_PTE_INDEX] = PA_TO_PTE_PPN(UART_PHYS_BASE) | PAGE_MMIO;

    // 4. Turn on MMU, Write to the satp register, Flush the TLB with sfence.vma 
    uart_puts("\n[Kernel] Before enable MMU ...\n");
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(MAKE_SATP(pgd))
        : "memory"
    );
    UART_BASE += PAGE_OFFSET;
    uart_puts("\n[Kernel] After enable MMU ...\n");
}

void drop_identity_map() {
    // 1. Zero out the low-half PGD entries
    pgd[IDENTITY_PGD_INDEX]     = 0;
    pgd[IDENTITY_PGD_INDEX + 1] = 0; // 如果沒加這行就沒問題 05/19 21:25 現在問題就是 
    // 2. Flush the TLB with sfence.vma
    asm volatile("sfence.vma zero, zero\n"); 
}

void vm_init() {
    drop_identity_map();
}

// ======================================================================================================================

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
void relocate_bootloader(unsigned long hartid) {
    unsigned long target_addr = 0x20000000;

    // 1. 取得「當前真正在執行的實體位址 (PC)」
    // auipc 配合 0，可以把當前這行指令的絕對位址存進 current_pc 變數
    unsigned long current_pc;
    asm volatile("auipc %0, 0" : "=r"(current_pc));

    // 2. 判斷我們是否已經在「新家」
    // 如果 PC 已經大於等於 0x20000000，代表我們已經跳轉過了！
    if (current_pc >= target_addr) {
        return; // 這裡的 return，會回到「新家」的 start_kernel 繼續往下執行！
    }

    // 3. 開始搬家 (從 Linker 規定的起點搬)
    unsigned long size = (unsigned long)_image_end - (unsigned long)_start;
    unsigned long *src = (unsigned long *)_start;
    unsigned long *dst = (unsigned long *)target_addr;
    
    for (unsigned long i = 0; i < size / 8; i++) {
        dst[i] = src[i];
    }

    // 4. 計算接下來要跳轉的「新 start_kernel」位址與「新 Stack」
    unsigned long offset = target_addr - (unsigned long)_start;
    unsigned long new_start_kernel = (unsigned long)&start_kernel + offset;
    unsigned long new_sp = (unsigned long)_end + offset;

    // 5. 信仰之躍！切換 Stack，並「重新呼叫」新家的 start_kernel
    asm volatile(
        "mv a0, %0\n"       // 帶上 hartid
        "mv sp, %1\n"       // 切換到新家的 Stack
        "jr %2\n"           // 跳轉到新家的 start_kernel 起點
        :
        : "r"(hartid), "r"(new_sp), "r"(new_start_kernel)
        : "a0", "memory"
    );

    while(1); // 永遠不會執行到這
}

void start_kernel(unsigned long hartid) {
    // Lab6: Basic Exercise - Virtual Memory in Kernel Space
    vm_init();
    uart_puts("\n[Kernel] Kernel Started!\n");

    boot_cpu_hartid = hartid;
    if ((unsigned long)fdt_ptr < PAGE_OFFSET) {
        fdt_ptr = (void *)((unsigned long)fdt_ptr + PAGE_OFFSET);
    }

    uart_puts("\n[Kernel] FDT address: ");
    uart_hex(fdt_ptr);
    uart_puts("\n");

    if (UART_BASE < PAGE_OFFSET) {
        UART_BASE += PAGE_OFFSET; // 讓核心全面走高位址的 MMIO 映射
    }

    uart_puts("\n[kernel] MMU enabled !\n");

    devicetree_early_init(fdt_ptr);

    if (cpio_base && (unsigned long)cpio_base < PAGE_OFFSET) {
        cpio_base = (void *)((unsigned long)cpio_base + PAGE_OFFSET);
        cpio_end  = (void *)((unsigned long)cpio_end + PAGE_OFFSET);
    }

    /* initialize UART ring buffers before any UART I/O */
    // uart_buffer_init();
    // /* enable UART RX and TX interrupts at UART peripheral (PLIC not configured yet) */
    // // 讓 uart 本身可以發送中斷
    // uart_enable_irqs(1, 1);
    // /* configure PLIC for UART interrupt on this hart */
    // // 把 uart 的 interrupt 導到對應的 hart
    // uart_plic_init_for_hart();
    // /* enable supervisor external interrupt delivery */
    // // s-mode 開啟 external interrupt enable
    // uart_enable_external_interrupt();
    
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

    // uart_puts("\n--- Reserved Memory Regions (Lab 3: Advance Exercise 2 - Reserved Memory) ---\n");
    // uart_puts("Hardware Reserved:  0x00000000 - 0x00001000\n");
    // uart_puts("Kernel Image (Without mem_map): ");
    // uart_hex((unsigned long)_start);
    // uart_puts(" - ");
    // uart_hex((unsigned long)_end);
    // uart_puts("\n");
    // uart_puts("Device Tree (DTB):  ");
    // uart_hex((unsigned long)fdt_ptr);
    // uart_puts(" - ");
    // uart_hex((unsigned long)fdt_ptr + fdt_size);
    // uart_puts(" (size: ");
    // uart_hex(fdt_size);
    // uart_puts(")\n");

    // if (cpio_base) {
    //     uart_puts("Initramfs:          ");
    //     uart_hex((unsigned long)cpio_base);
    //     uart_puts(" - ");
    //     uart_hex((unsigned long)cpio_end);
    //     uart_puts("\n");
    // }

    // for (int i = 0; i < fdt_reserved_regions_count; i++) {
    //     uart_puts("FDT Reserved:       ");
    //     uart_hex(fdt_reserved_regions[i].base);
    //     uart_puts(" - ");
    //     uart_hex(fdt_reserved_regions[i].base + fdt_reserved_regions[i].size);
    //     uart_puts(" (");
    //     uart_puts(fdt_reserved_regions[i].name);
    //     uart_puts(")\n");
    // }
    // uart_puts("-------------------------------\n");

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

    // ============ Lab5: Basic Exercise 1 - Thread ============

    struct task_struct* shell_task = thread_create(idle);
    uart_puts("\n[Kernel] Kernel shell task created.\n");

    shell_task->state = TASK_RUNNABLE; // 先設為 RUNNABLE
    remove_task_from_queue(&run_queue, shell_task); // 把它從隊列抽出來
    shell_task->state = TASK_RUNNING; // 宣告它正在執行

    asm volatile("mv tp, %0" : : "r"(shell_task));

    // for (int i = 0; i < 3; i++) {
    //     thread_create(foo);
    // }
    // idle();

    // =========================================================

    timer_init();

    // ========== Lab7: Virtual File System ==========
    vfs_init();
    
    struct vnode* root_vnode = NULL;
    if (vfs_lookup("/", &root_vnode) == 0) {
        shell_task->cwd = root_vnode; // 初始化根目錄給系統最初的 task
    }
    // ===============================================

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

            void *safe_fdt_ptr = fdt_ptr;
            
            uint32_t magic = 0; 
            uint32_t size = 0; 
            uint8_t * load_ptr = (uint8_t *)0x00200000; 
            
            for (uint8_t i = 0; i < 4; i ++) { 
                ((char *)&magic)[i] = uart_polling_getc();
            }

            if (magic != 0x544F4F42) {
                uart_puts("Error: Invalid Magic Number, stop loading.\n");
            } 
            else {
                uart_puts("Magic Number Correct! Receiving size...\n");
                for (uint8_t i = 0; i < 4; i ++) {
                    ((char *)&size)[i] = uart_polling_getc();
                }
                uart_puts("Get the size of the file.\n");
                for (uint32_t i = 0; i < size; i ++) {
                    load_ptr[i] = uart_polling_getc();
                    if (i % 4096 == 0) {
                        uart_puts(".");
                    }
                }
            
                void (*kernel_entry)(unsigned long, void *) = (void (*)(unsigned long, void *))0x00200000;
                uart_puts("Set program counter to 0x00200000 and passing FDT pointer...\n");
                kernel_entry(0, safe_fdt_ptr); // a0 & a1 register 
                // a0: 0 是 hartid ?
                // a1: 指向 fdt 的實體位址
                // On most RISC-V platforms, the firmware (OpenSBI or U-Boot) loads the devicetree 
                // address in register a1 before jumping to the kernel.
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
                struct task_struct* child;

                strncpy(current_exec_path, filename, sizeof(current_exec_path) - 1);
                current_exec_path[sizeof(current_exec_path) - 1] = '\0';

                child = thread_create(run_user_program);
                if (!child) {
                    uart_puts("Failed to create task for exec.\n");
                } else {
                    sys_waitpid(child->pid);
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
