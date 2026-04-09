#include <iostream>
#include <list>
#include <vector>

#define NUM_PAGES 0x280000 // \approx 2600 * 2^10
#define MAX_ORDER 10

struct page {
    int order = 0;
    int refcount = 0; // 紀錄多少人在使用這一塊 page
};

// 紀錄每一個 page 的狀態
std::vector<page> mem_map;

// upward: means order 2 -> 3 -> 4 in free_area
// eg. free_area[2] 會儲存所有可以提供 4 個 pages 的 memory address
std::vector<std::list<page*>> free_area;

// 你有一個 order 5 的區塊 切一半變成兩個 order 4
// 前半段仍然是 p 
// 後半段就是 get_buddy(p, 4)
struct page* get_buddy(struct page* page, unsigned int order) {
    return &mem_map[(page - mem_map.data()) ^ (1 << order)];
}

struct page* alloc_pages(unsigned int order) {
    // TODO: Implement this function
    
    struct page* ret = nullptr;

    // Case I: 如果這個 order 剛好有 直接分配
    if (free_area[order].size() >= 1) {
        ret = free_area[order].front();
        ret -> refcount ++;
        // remove that page from free_area[order]
        free_area[order].pop_front();
    } 
    else { // Case II: 這個 order 不夠 要去前面的 order 拿
        struct page* alloc = nullptr;
        for (size_t i = order + 1; i <= MAX_ORDER; i ++) {
            if (free_area[i].size() >= 1) {
                alloc = free_area[i].front();
                free_area[i].pop_front();
                break;
            }
        }

        // Case II-I: 找到可以拆分的 order page
        if (alloc) {
            while (alloc -> order != order) {
                // 1. 先把 alloc -> order - 1;
                alloc -> order --;

                // 2. 把 alloc 一半以後的 page order 也 - 1; 加入到 free_area[order - 1] (後半)
                struct page* buddy = get_buddy(alloc, alloc -> order);
                buddy -> order = alloc -> order; // buddy 有可能沒有被設定過 order 不能用 --
                free_area[alloc -> order].push_back(buddy);

                // 3. 檢查以 alloc 起始的 page 的 order (前半) 現在是不是 user 要求的
                // 4. 是的話 分配給 user (改 refcount)
                if (alloc -> order == order) {
                    ret = alloc;
                    ret -> refcount ++;

                    break;
                }
                // 5. 不是的話 重新回到 1.
            }
        }
        else { // Case II-II: 沒有找到可以拆分的 order page 
            // return nullptr
            return ret;
        }
    }

    return ret;
}

void free_pages(struct page* page) {
    // TODO: Implement this function
    
    // 1. 把當前這個 page refcount --
    page -> refcount = 0;
    int order = page -> order;

    // 3. while loop if order < MAX_ORDER
    while (order < MAX_ORDER) {
        // 4. 檢查 buddy 只有物理位置相鄰 且大小相同的才能合併
        struct page* buddy = get_buddy(page, order);
        // 需要合併
        if (buddy -> refcount == 0 && buddy -> order == order) { // 如果不判斷 order 呢？
            free_area[order].remove(buddy); // O(n)
            page = (page < buddy) ? page : buddy;
            order ++;
        }
        else { // 不用合併 代表已經完成
            break;
        }
    }
    page -> order = order;
    free_area[order].push_back(page);
    return;
}

void dump() {
    for (int i = MAX_ORDER; i >= 0; i--)
        std::cout << "free_area[" << i << "] " << free_area[i].size()
                  << std::endl;
}

int main() {
    mem_map.resize(NUM_PAGES);
    free_area.resize(MAX_ORDER + 1);
   
    // 一開始把所有 page 都先歸類到 2^10 order 的
    for (size_t i = 0; i < NUM_PAGES; i += (1 << MAX_ORDER)) {
        mem_map[i].order = MAX_ORDER;
        free_area[MAX_ORDER].push_back(&mem_map[i]);
    }

    std::cout << "\np1:\n";
    struct page* p1 = alloc_pages(1);
    dump();

    std::cout << "\np2:\n";
    struct page* p2 = alloc_pages(1);
    dump();

    std::cout << "\np3:\n";
    struct page* p3 = alloc_pages(1);
    dump();

    free_pages(p1);
    free_pages(p2);
    free_pages(p3);

    std::cout << "\nfree:\n";
    dump();
    return 0;
}
