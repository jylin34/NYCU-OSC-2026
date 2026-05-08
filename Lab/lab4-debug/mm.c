#include "stdint.h"
#include "stddef.h"
#include "list.h"

/* 安全遍歷巨集：支援在迴圈中 list_del 節點 */
#ifndef list_for_each_safe
#define list_for_each_safe(curr, next, head) \
    for (curr = (head)->next, next = curr->next; curr != (head); \
         curr = next, next = curr->next)
#endif

/* 外部 UART 與 FDT 宣告 */
extern void uart_puts(const char* s);
extern void uart_putc(char c);
extern void uart_hex(unsigned long h);
extern void* fdt_ptr;
extern unsigned long fdt_size;
extern void* cpio_base;
extern void* cpio_end;
extern unsigned long mem_base;
extern unsigned long mem_size;

struct reserved_region {
    unsigned long base;
    unsigned long size;
    char name[32];
};
extern struct reserved_region fdt_reserved_regions[];
extern int fdt_reserved_regions_count;

/* Linker symbols */
extern char _start[];
extern char _end[];

#define PAGE_SIZE 4096 
#define MAX_ORDER 10

struct page {
    // circular doubly lniked list
    struct list_head list;
    int order;    
    int refcount;

    // for slab layer 
    int is_slab;
    uint32_t slab_id;
};

/* --- 全局變數 --- */
struct page* mem_map; 
unsigned long buddy_memory_base;
unsigned long buddy_num_pages;
struct list_head free_area[MAX_ORDER + 1];
static int mm_log_enabled = 0;

/* --- 內部輔助函式 --- */
static void uart_dec(unsigned long n) {
    if (n == 0) {
        uart_putc('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    while (--i >= 0)
        uart_putc(buf[i]);
}

static void print_size(unsigned long bytes) {
    uart_dec(bytes);
    uart_puts(" Bytes (");
    if (bytes >= 1024 * 1024) {
        uart_dec(bytes / (1024 * 1024));
        uart_puts(" MB)");
    } else if (bytes >= 1024) {
        uart_dec(bytes / 1024);
        uart_puts(" KB)");
    } else {
        uart_puts("B)");
    }
}

/* --- Startup Allocator (Advanced 3) --- */
/* 從 Kernel 結尾 (_end) 開始分配 */
static char* startup_alloc_ptr = _end; 

// size 要分配的記憶體大小 （也就是 buddy system mem_map 要多大）
void* startup_alloc(size_t size, size_t alignment) {
    /* 1. 執行位址對齊 */
    startup_alloc_ptr = (char*)(((unsigned long)startup_alloc_ptr + alignment - 1) & ~(alignment - 1));
    
    /* 2. 迴圈檢查是否與保留區重疊，若重疊則「跳過」該區塊 */
    // 以 size 為一個區塊 一個區塊 一個區塊 檢查
    while (1) {
        void* start = startup_alloc_ptr;
        void* end = (char*)startup_alloc_ptr + size;
        int overlapped = 0;

        /* 檢查與 硬體保留區 (0x0-0x1000) 是否重疊 */
        if (!((unsigned long)end <= 0x0 || (unsigned long)start >= 0x1000)) {
            startup_alloc_ptr = (char*)0x1000;
            overlapped = 1;
        }
        /* 檢查與 FDT (Device Tree Blob) 是否重疊 */
        if (fdt_ptr && fdt_size > 0) {
            unsigned long r_start = (unsigned long)fdt_ptr;
            unsigned long r_end = r_start + fdt_size;
            if (!((unsigned long)end <= r_start || (unsigned long)start >= r_end)) {
                startup_alloc_ptr = (char*)r_end;
                overlapped = 1;
            }
        }
        /* 檢查與 Initramfs (CPIO) 是否重疊 */
        if (cpio_base && cpio_end) {
            unsigned long r_start = (unsigned long)cpio_base;
            unsigned long r_end = (unsigned long)cpio_end;
            if (!((unsigned long)end <= r_start || (unsigned long)start >= r_end)) {
                startup_alloc_ptr = (char*)r_end;
                overlapped = 1;
            }
        }
        /* 檢查與 Device Tree 中定義的其他保留區是否重疊 */
        for (int j = 0; j < fdt_reserved_regions_count; j++) {
            unsigned long r_start = fdt_reserved_regions[j].base;
            unsigned long r_end = r_start + fdt_reserved_regions[j].size;
            if (!((unsigned long)end <= r_start || (unsigned long)start >= r_end)) {
                startup_alloc_ptr = (char*)r_end;
                overlapped = 1;
                break;
            }
        }

        /* 若無重疊則跳出，否則重新執行對齊並再次檢查 */
        if (!overlapped) break;
        startup_alloc_ptr = (char*)(((unsigned long)startup_alloc_ptr + alignment - 1) & ~(alignment - 1));
    }

    /* 3. 確定安全位址，移動指標並回傳 */
    void* addr = startup_alloc_ptr;
    startup_alloc_ptr += size;
    return addr;
}

/* --- 地址轉換 --- */
static inline void* page_to_addr(struct page* p) {
    unsigned long index = p - mem_map;
    return (void*)(buddy_memory_base + index * PAGE_SIZE);
}

static inline struct page* addr_to_page(void* addr) {
    unsigned long offset = (unsigned long)addr - buddy_memory_base;
    unsigned long index = offset / PAGE_SIZE;
    return &mem_map[index];
}

static inline struct page* get_buddy(struct page* p, int order) {
    unsigned long index = p - mem_map;
    unsigned long buddy_index = index ^ (1 << order);
    if (buddy_index >= buddy_num_pages) return NULL;
    return &mem_map[buddy_index];
}

void buddy_info();

void memory_reserve(unsigned long base, unsigned long size) {
    if (size == 0) return;

    unsigned long mem_end = buddy_memory_base + buddy_num_pages * PAGE_SIZE;
    if (base >= mem_end || (base + size) <= buddy_memory_base) {
        return; 
    }

    unsigned long actual_start = (base < buddy_memory_base) ? buddy_memory_base : base;
    unsigned long actual_end = (base + size > mem_end) ? mem_end : (base + size);

    // 計算該位址對應到的 page frame number
    unsigned long start_pfn = (actual_start - buddy_memory_base) / PAGE_SIZE;
    unsigned long end_pfn = (actual_end - buddy_memory_base + PAGE_SIZE - 1) / PAGE_SIZE;

    if (mm_log_enabled) {
        uart_puts("[Reserve] Reserve address [");
        uart_hex(base);
        uart_puts(", ");
        uart_hex(base + size);
        uart_puts("). Range of pages: [");
        uart_dec(start_pfn);
        uart_puts(", ");
        uart_dec(end_pfn);
        uart_puts(")\n");
    }

    // 從 MAX_ORDER 開始檢查，檢查 free_area 裡面的每一塊是否與 reserved area 重疊.
    for (int order = MAX_ORDER; order >= 0; order--) {
        struct list_head *curr, *next;
        // 安全遍歷
        // 傳統上如果在 for loop 裡面 list_del 會倒值無法 access curr -> next 
        // 安全遍歷會備份一個臨時的 curr -> next
        list_for_each_safe(curr, next, &free_area[order]) {
            struct page *p = list_entry(curr, struct page, list);
            unsigned long pfn = p - mem_map;
            unsigned long block_start = pfn;
            unsigned long block_end = pfn + (1 << order);
            
            // Case I: 完全沒重疊
            if (block_end <= start_pfn || block_start >= end_pfn) {
                continue;
            }

            list_del(curr);
            if (mm_log_enabled) {
                uart_puts("[-] Remove page ");
                uart_dec(pfn);
                uart_puts(" from order ");
                uart_dec(order);
                uart_puts(". Range: [");
                uart_dec(pfn);
                uart_puts(", ");
                uart_dec(pfn + (1 << order) - 1);
                uart_puts("]\n");
            }
                                            
            // Case II: 完全重疊
            if (block_start >= start_pfn && block_end <= end_pfn) {
                p->refcount = 1; // 標記為已經被佔用
                continue;
            }
            
            // Case III: 部分重疊
            // 原本一大塊 不斷地拆成兩小塊 去做檢查
            if (order > 0) {
                int next_order = order - 1;
                struct page* buddy = get_buddy(p, next_order);

                p->order = next_order;
                buddy->order = next_order;
                p->refcount = 0;
                buddy->refcount = 0;

                list_add_tail(&p->list, &free_area[next_order]);
                if (mm_log_enabled) {
                    uart_puts("[+] Add page ");
                    uart_dec(p - mem_map);
                    uart_puts(" to order ");
                    uart_dec(next_order);
                    uart_puts(". Range: [");
                    uart_dec(p - mem_map);
                    uart_puts(", ");
                    uart_dec((p - mem_map) + (1 << next_order) - 1);
                    uart_puts("]\n");
                }

                list_add_tail(&buddy->list, &free_area[next_order]);
                if (mm_log_enabled) {
                    uart_puts("[+] Add page ");
                    uart_dec(buddy - mem_map);
                    uart_puts(" to order ");
                    uart_dec(next_order);
                    uart_puts(". Range: [");
                    uart_dec(buddy - mem_map);
                    uart_puts(", ");
                    uart_dec((buddy - mem_map) + (1 << next_order) - 1);
                    uart_puts("]\n");
                }
            } else {
                p->refcount = 1;
            }
        }
    }
}

void slab_init();

void mm_init(unsigned long base, unsigned long size) {
    uart_puts("\n--- Initializing Buddy System (Lab 3: Advanced Exercise 3 - Startup Allocator) ---\n");
    uart_puts("Total Physical Memory: ");
    print_size(size);
    uart_puts("\nBase Address:          ");
    uart_hex(base);
    uart_puts("\n");

    buddy_memory_base = base;
    buddy_num_pages = size / PAGE_SIZE;

    for (int i = 0; i <= MAX_ORDER; i++) {
        list_init(&free_area[i]);
    }

    size_t mem_map_size = buddy_num_pages * sizeof(struct page);
    mem_map = (struct page*)startup_alloc(mem_map_size, 4096);
    
    uart_puts("mem_map (Frame Array): allocated at "); 
    uart_hex((unsigned long)mem_map);
    uart_puts(", size: ");
    print_size(mem_map_size);
    uart_puts("\n");

    for (unsigned long i = 0; i < buddy_num_pages; i++) {
        list_init(&mem_map[i].list);
        mem_map[i].order = -1;
        mem_map[i].refcount = 0;
    }

    // 負責將系統所有可用的記憶體，按照「最
    // 大可能塊」的原則，填入 Buddy System 的
    // free_area（空閒鏈結串列）中
    unsigned long i = 0;
    while (i < buddy_num_pages) {
        int order = MAX_ORDER;
        while ((1 << order) > (buddy_num_pages - i)) {
            order--;
        }
        struct page* p = &mem_map[i];
        p->order = order;
        list_add_tail(&p->list, &free_area[order]);
        i += (1 << order);
    }

    uart_puts("Marking reserved regions...\n");

    // Prevent NULL Pointer (0 represents NULL in C)
    // or Prevent OpenSBI / Spin Tables
    uart_puts("  Reserve Hardware:                 0x00000000 - 0x00001000 (Size: ");
    print_size(0x1000);
    uart_puts(")\n");
    memory_reserve(buddy_memory_base, 0x1000); 

    unsigned long kernel_reserve_size = (unsigned long)startup_alloc_ptr - (unsigned long)_start;
    uart_puts("  Reserve Kernel (Include mem_map): "); 
    uart_hex((unsigned long)_start);
    uart_puts(" - "); 
    uart_hex((unsigned long)startup_alloc_ptr);
    uart_puts(" (Size: ");
    print_size(kernel_reserve_size);
    uart_puts(")\n");
    memory_reserve((unsigned long)_start, kernel_reserve_size);

    if (fdt_ptr && fdt_size > 0) {
        uart_puts("  Reserve DTB:                      "); 
        uart_hex((unsigned long)fdt_ptr);
        uart_puts(" - "); 
        uart_hex((unsigned long)fdt_ptr + fdt_size);
        uart_puts(" (Size: ");
        print_size(fdt_size);
        uart_puts(")\n");
        memory_reserve((unsigned long)fdt_ptr, fdt_size);
    }

    if (cpio_base && cpio_end) {
        unsigned long cpio_size = (unsigned long)cpio_end - (unsigned long)cpio_base;
        uart_puts("  Reserve Initrd:                   "); 
        uart_hex((unsigned long)cpio_base);
        uart_puts(" - "); 
        uart_hex((unsigned long)cpio_end);
        uart_puts(" (Size: ");
        print_size(cpio_size);
        uart_puts(")\n");
        memory_reserve((unsigned long)cpio_base, cpio_size);
    }

    for (int j = 0; j < fdt_reserved_regions_count; j++) {
        uart_puts("  Reserve FDT Region ("); 
        uart_puts(fdt_reserved_regions[j].name);
        uart_puts("): ");
        uart_hex(fdt_reserved_regions[j].base);
        uart_puts(" - "); 
        uart_hex(fdt_reserved_regions[j].base + fdt_reserved_regions[j].size);
        uart_puts(" (Size: ");
        print_size(fdt_reserved_regions[j].size);
        uart_puts(")\n");
        memory_reserve(fdt_reserved_regions[j].base, fdt_reserved_regions[j].size);
    }

    uart_puts("Buddy System initialization complete.\n");
    slab_init();
    buddy_info();
    uart_puts("----------------------------------------------------------\n");
}

// === Buddy System ===

// buddy system - allocate page 
struct page* alloc_pages(int order) {
    struct page* ret = NULL;
    // Case I: 如果這個 order 剛好有 直接分配
    if (!list_empty(&free_area[order])) {
        struct list_head *node = free_area[order].next;
        ret = list_entry(node, struct page, list);
        list_del(node);

        if (mm_log_enabled) {
            uart_puts("[-] Remove page ");
            uart_dec(ret - mem_map);
            uart_puts(" from order ");
            uart_dec(order);
            uart_puts(". Range: [");
            uart_dec(ret - mem_map);
            uart_puts(", ");
            uart_dec((ret - mem_map) + (1 << order) - 1);
            uart_puts("]\n");
        }

        ret->refcount = 1;
    } else {
        // Case II: 這個 order 不夠 要去前面的 order 拿
        struct page* alloc = NULL;
        for (int i = order + 1; i <= MAX_ORDER; i++) {
            if (!list_empty(&free_area[i])) {
                struct list_head *node = free_area[i].next;
                alloc = list_entry(node, struct page, list);
                list_del(node);

                if (mm_log_enabled) {
                    uart_puts("[-] Remove page ");
                    uart_dec(alloc - mem_map);
                    uart_puts(" from order ");
                    uart_dec(i);
                    uart_puts(". Range: [");
                    uart_dec(alloc - mem_map);
                    uart_puts(", ");
                    uart_dec((alloc - mem_map) + (1 << i) - 1);
                    uart_puts("]\n");
                }
                break;
            }
        }
        // Case II-I: 找到可以拆分的 order page
        if (alloc) {
            while (alloc->order > order) {
                // 1. 先把 alloc -> order - 1;
                alloc->order--;
                // 2. 把 alloc 一半以後的 page order 也 - 1; 加入到 free_area[order - 1] (後半)
                struct page* buddy = get_buddy(alloc, alloc->order);
                buddy->order = alloc->order;
                buddy->refcount = 0;
                list_add(&buddy->list, &free_area[buddy->order]);

                if (mm_log_enabled) {
                    uart_puts("[+] Add page ");
                    uart_dec(buddy - mem_map);
                    uart_puts(" to order ");
                    uart_dec(buddy->order);
                    uart_puts(". Range: [");
                    uart_dec(buddy - mem_map);
                    uart_puts(", ");
                    uart_dec((buddy - mem_map) + (1 << buddy->order) - 1);
                    uart_puts("]\n");
                }
            }
            // 3. 檢查以 alloc 起始的 page 的 order (前半) 現在是不是 user 要求的
            // 4. 是的話 分配給 user (改 refcount)
            ret = alloc;
            ret->refcount = 1;
            // Case II-II: 沒有找到可以拆分的 order page 
        }
    }

    if (ret && mm_log_enabled) {
        buddy_info();
    }
    return ret;
}

// buddy system - free pages
void free_pages(struct page* p) {
    if (!p) return;
    if (p->refcount == 0) {
        uart_puts("Error: Double free detected on page ");
        uart_dec(p - mem_map);
        uart_puts("!\n");
        return;
    }
    // 1. 把當前這個 page refcount --
    p->refcount = 0;
    int order = p->order;
    // 3. while loop if order < MAX_ORDER
    while (order < MAX_ORDER) {
        // 4. 檢查 buddy 只有物理位置相鄰 且大小相同的才能合併
        struct page* buddy = get_buddy(p, order);
        // 需要合併
        if (buddy && buddy->refcount == 0 && buddy->order == order) {
            if (mm_log_enabled) {
                uart_puts("[*] Buddy found! buddy idx: ");
                uart_dec(buddy - mem_map);
                uart_puts(" for page ");
                uart_dec(p - mem_map);
                uart_puts(" with order ");
                uart_dec(order);
                uart_puts("\n");
            }

            list_del(&buddy->list);

            if (mm_log_enabled) {
                uart_puts("[-] Remove page ");
                uart_dec(buddy - mem_map);
                uart_puts(" from order ");
                uart_dec(order);
                uart_puts(". Range: [");
                uart_dec(buddy - mem_map);
                uart_puts(", ");
                uart_dec((buddy - mem_map) + (1 << order) - 1);
                uart_puts("]\n");
            }

            buddy->order = -1;
            // 確保指標指向合併後區塊的起始位址
            p = (p < buddy) ? p : buddy;
            order++;
            p->order = order;
        } else break; // 不用合併 代表已經完成
    }
    p->order = order;
    list_add(&p->list, &free_area[order]);

    if (mm_log_enabled) {
        uart_puts("[+] Add page ");
        uart_dec(p - mem_map);
        uart_puts(" to order ");
        uart_dec(order);
        uart_puts(". Range: [");
        uart_dec(p - mem_map);
        uart_puts(", ");
        uart_dec((p - mem_map) + (1 << order) - 1);
        uart_puts("]\n");
        buddy_info();
    }
}

void buddy_info() {
    unsigned long total_free = 0;
    uart_puts("--- Buddy System Free List Status ---\n");
    for (int i = MAX_ORDER; i >= 0; i--) {
        unsigned long count = 0;
        struct list_head *curr;
        for (curr = free_area[i].next; curr != &free_area[i]; curr = curr->next) {
            count++;
        }
        uart_puts("Order ");
        if (i < 10) uart_putc(' ');
        uart_dec(i);
        uart_puts(": ");
        uart_dec(count);
        uart_puts(" blocks\n");
        
        total_free += count * (1UL << i) * PAGE_SIZE;
    }
    uart_puts("Total Free Memory: ");
    print_size(total_free);
    uart_puts("\n");
}

void buddy_test() {
    uart_puts("\n--- Starting Buddy System Test ---\n");
    struct page* p1 = alloc_pages(0);
    if (p1) {
        uart_puts("Allocated p1 at: ");
        uart_hex((unsigned long)page_to_addr(p1));
        uart_puts("\nFreeing p1...\n");
        free_pages(p1);
    }
    uart_puts("--- Buddy System Test Complete ---\n");
}

// === kmalloc (slab layer) ===

#define NUM_POOLS 9 // for slab layer: 8, 16, 32, 64, 128, 256, 512, 1024, 2048 Bytes

static const size_t pool_sizes[NUM_POOLS] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048 
};

struct slab_pool {
    size_t obj_size;
    void *free_head;
};

struct slab_pool pools[NUM_POOLS];

// kmalloc (slab layer) - init 
void slab_init() {
    for (size_t i = 0; i < NUM_POOLS; i ++) {
        pools[i].obj_size = pool_sizes[i];
        pools[i].free_head = NULL;
    }
}
// kmalloc (slab layer) - alloc
void * slab_alloc(size_t size) {
    // 先看 nearest size 是多少
    int32_t idx = -1;
    for (size_t i = 0; i < NUM_POOLS; i ++) {
        if (size <= pools[i].obj_size) {
            idx = i;
            break;
        } 
    }
    if (idx == -1)  return NULL;
    // 檢查對應的 pool 有沒有
    struct slab_pool * pool = &pools[idx];
    // 沒有的話要去 alloc 一個新的 page
    if (pool -> free_head == NULL) {
        struct page * p_new = alloc_pages(0);
        if (p_new == NULL)  return NULL;
        
        p_new -> is_slab = 1;
        p_new -> slab_id = idx;

        void * page_addr = page_to_addr(p_new);
        size_t obj_size = pool -> obj_size;

        if (mm_log_enabled) {
            uart_puts("[Chunk] New page for slab size ");
            uart_dec(obj_size);
            uart_puts(" at ");
            uart_hex((unsigned long)page_addr);
            uart_puts("\n");
        }

        // 把這一個 page 切分成好多個 slice
        for (size_t offset = 0; offset + obj_size <= PAGE_SIZE; offset += obj_size) {
            // Embedded Linked List
            void** current = (void**)((unsigned long)page_addr + offset);
            if (offset + obj_size * 2 <= PAGE_SIZE) {
                *current = (void*)((unsigned long)page_addr + offset + obj_size);
            } else {
                *current = NULL; // 最後一個 chunk
            }
        }
        pool -> free_head = page_addr;
    }
    // 從 pool 裡面 free_head 拿一個 slice 出來
    void *ptr = pool -> free_head;
    pool -> free_head = *(void **)ptr;

    if (mm_log_enabled) {
        uart_puts("[Chunk] Allocate ");
        uart_hex((unsigned long)ptr);
        uart_puts(" at chunk size ");
        uart_dec(pool->obj_size);
        uart_puts("\n");
    }

    return ptr;
}
// kmalloc (slab layer) - free
void slab_free(void * ptr) {
    if (ptr == NULL) return; 
    struct page * p = addr_to_page(ptr);
    if (p->is_slab == 0) {
        uart_puts("Error: slab_free called on a non-slab page!\n");
        return;
    }
    // 取得這個 page 對應到的 slab layer pool 
    uint32_t idx = p -> slab_id;
    struct slab_pool * pool = &pools[idx];

    if (mm_log_enabled) {
        uart_puts("[Chunk] Free ");
        uart_hex((unsigned long)ptr);
        uart_puts(" at chunk size ");
        uart_dec(pool->obj_size);
        uart_puts("\n");
    }

    // 回收 slice chunk
    *(void **)ptr = pool -> free_head;
    pool -> free_head = ptr;
}
// memory management - allocate memory
void * allocate(size_t size) {
    if (size == 0)  return NULL;
    // 判斷 size 大小 要用 buddy system or slab layer 
    // Use slab layer
    if (size <= 2048) {
        return slab_alloc(size);
    }
    // Use Buddy System
    int order = 0;
    while ((PAGE_SIZE << order) < size) {
        order ++;
        // size too large
        if (order > MAX_ORDER) {
            return NULL;
        }
    }
    struct page * p = alloc_pages(order);
    if (p == NULL)  return NULL;
    // mark this page is not slab page 
    p -> is_slab = 0;

    void * addr = page_to_addr(p);
    if (mm_log_enabled) {
        uart_puts("[Page] Allocate ");
        uart_hex((unsigned long)addr);
        uart_puts(" at order ");
        uart_dec(order);
        uart_puts(", page ");
        uart_dec(p - mem_map);
        
        if (!list_empty(&free_area[order])) {
            struct page *next_p = list_entry(free_area[order].next, struct page, list);
            uart_puts(". Next address at order ");
            uart_dec(order);
            uart_puts(": ");
            uart_hex((unsigned long)page_to_addr(next_p));
        } else {
            uart_puts(". Next address at order ");
            uart_dec(order);
            uart_puts(": None");
        }
        uart_puts("\n");
    }

    return addr;
}
// memory management - free memory 
void free(void * ptr) {
    // 判斷 size 大小 要用 buddy system or slab layer
    if (ptr == NULL)    return;
    struct page * p = addr_to_page(ptr);
    if (p -> is_slab == 1) {
        // free slices from slab layer
        slab_free(ptr);
    }
    else {
        unsigned long addr = (unsigned long)ptr;
        // free pages from Buddy System
        free_pages(p);

        if (mm_log_enabled) {
            uart_puts("[Page] Free ");
            uart_hex(addr);
            uart_puts(" and add back to order ");
            uart_dec(p->order);
            uart_puts(", page ");
            uart_dec(p - mem_map);
            
            if (!list_empty(&free_area[p->order])) {
                 struct page *next_p = list_entry(free_area[p->order].next, struct page, list);
                 uart_puts(". Next address at order ");
                 uart_dec(p->order);
                 uart_puts(": ");
                 uart_hex((unsigned long)page_to_addr(next_p));
            } else {
                 uart_puts(". Next address at order ");
                 uart_dec(p->order);
                 uart_puts(": None");
            }
            uart_puts("\n");
        }
    }
}

// Lab 3 Demo test case

#define MAX_ALLOC_SIZE (PAGE_SIZE << MAX_ORDER)

void test_alloc_1() {
    mm_log_enabled = 1;
    /***************** Case 1 *****************/

uart_puts("\n===== Part 1 =====\n");

void *p1 = allocate(4097);
free(p1);

uart_puts("\n=== Part 1 End ===\n");

uart_puts("\n===== Part 2 =====\n");

// Allocate all blocks at order 0, 1, 2 and 3
int NUM_BLOCKS_AT_ORDER_0 = 2;  // Need modified
int NUM_BLOCKS_AT_ORDER_1 = 1;
int NUM_BLOCKS_AT_ORDER_2 = 1;
int NUM_BLOCKS_AT_ORDER_3 = 0;

void *ps0[NUM_BLOCKS_AT_ORDER_0];
void *ps1[NUM_BLOCKS_AT_ORDER_1];
void *ps2[NUM_BLOCKS_AT_ORDER_2];
void *ps3[NUM_BLOCKS_AT_ORDER_3];
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_0; ++i) {
    ps0[i] = allocate(4096);
}
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_1; ++i) {
    ps1[i] = allocate(8192);
}
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_2; ++i) {
    ps2[i] = allocate(16384);
}
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_3; ++i) {
    ps3[i] = allocate(32768);
}

uart_puts("\n-----------\n");

long MAX_BLOCK_SIZE = PAGE_SIZE * (1 << MAX_ORDER);

/* **DO NOT** uncomment this section */
void *p2, *p3, *p4, *p5, *p6, *p7, *p8, *p9, *p10;

p1 = allocate(4095);
free(p1);                        // 4095
p1 = allocate(4095);

p2 = allocate(3769);
p3 = allocate(2699);
p4 = allocate(1028);
p5 = allocate(1);
p6 = allocate(4096);
free(p5);                        // 1
p7 = allocate(16000);
free(p1);                        // 4095
free(p4);                        // 1028
free(p2);                        // 3769
p8 = allocate(4097);
p9 = allocate(MAX_BLOCK_SIZE + 1);
p10 = allocate(MAX_BLOCK_SIZE);
free(p6);                        // 4096
free(p8);                        // 4097
p2 = allocate(7197);

free(p10);                       // MAX_BLOCK_SIZE
free(p7);                        // 16000
free(p2);                        // 7197
free(p3);                        // 2699

uart_puts("\n-----------\n");

// Free all blocks remaining
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_0; ++i) {
    free(ps0[i]);
}
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_1; ++i) {
    free(ps1[i]);
}
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_2; ++i) {
    free(ps2[i]);
}
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_3; ++i) {
    free(ps3[i]);
}

uart_puts("\n=== Part 2 End ===\n");
}
