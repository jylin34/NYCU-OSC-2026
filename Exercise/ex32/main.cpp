#include <iostream>
#include <list>
#include <vector>

#define PAGE_SIZE (1UL << 12) // 4096 Bytes = 4KB
#define NUM_PAGES 0x280000
#define MAX_ORDER 10

typedef unsigned long phys_addr_t;

struct page {
    int order = 0;
    int refcount = 0;
};

std::vector<page> mem_map;
std::vector<std::list<page*>> free_area;

struct page* get_buddy(struct page* page, unsigned int order) {
    return &mem_map[(page - mem_map.data()) ^ (1 << order)];
}

// 要保護哪一塊地？
// base ~ base + size 
void memory_reserve(phys_addr_t base, size_t size) {
    // TODO: Implement this function
    if (size == 0) return;

    size_t start_pfn = base / PAGE_SIZE;
    // 找出在 reserved area 裡面的最後一個 page 的編號
    size_t end_pfn = (base + size + PAGE_SIZE - 1) / PAGE_SIZE;

    // Top-Down
    // 如果用 size_t 遇到 0-- 會變成超大的數字
    for (int order = MAX_ORDER; order >= 0; order --) {
        auto it = free_area[order].begin();
        while (it != free_area[order].end()) {
            struct page *p = *it;
            size_t pfn = p - mem_map.data(); // 代表現在處理的是哪一個 page
            size_t block_start = pfn;
            size_t block_end = pfn + (1 << order);
            
            // 完全沒有 overlap
            if (block_end <= start_pfn || block_start >= end_pfn) {
                it ++;
                continue;
            }

            it = free_area[order].erase(it); // O(n)
                                             
            if (block_start >= start_pfn && block_end <= end_pfn) {
                p -> refcount = 1;
                continue;
            }

            int next_order = order - 1;
            struct page* buddy = get_buddy(p, next_order);

            p -> order = next_order;
            buddy -> order = next_order;

            free_area[next_order].push_back(p);
            free_area[next_order].push_back(buddy);
        }
    }
}

void dump() {
    for (int i = MAX_ORDER; i >= 0; i--)
        std::cout << "free_area[" << i << "] " << free_area[i].size()
                  << std::endl;
}

void mm_init() {
    mem_map.resize(NUM_PAGES);
    free_area.resize(MAX_ORDER + 1);
    for (size_t i = 0; i < NUM_PAGES; i += (1 << MAX_ORDER)) {
        mem_map[i].order = MAX_ORDER;
        free_area[MAX_ORDER].push_back(&mem_map[i]);
    }
    // pfn = Page Frame Number
    // start_pfn = 0 / end_pfn = 0x82a69510
    memory_reserve(0, 0x82a69510); 
}

int main() {
    mm_init();
    dump();
    return 0;
}
