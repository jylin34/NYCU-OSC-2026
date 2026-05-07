#include <stdint.h>
#include <stddef.h>

// https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html#header 5.4.1
#define FDT_BEGIN_NODE 0x00000001 // 一個節點的開始
#define FDT_END_NODE   0x00000002 // 一個節點的結束
#define FDT_PROP       0x00000003 // 屬性開始
#define FDT_NOP        0x00000004 // 無操作 parsing 的時候會直接跳過 通常用於對齊或填充空間
#define FDT_END        0x00000009 // 整個 FDT 檔案的資料區段已經結束 後面不會再有任何 token

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

void *fdt_ptr;

uint32_t bswap32(uint32_t x) {
    return ((x & 0x000000ff) << 24) |
           ((x & 0x0000ff00) << 8)  |
           ((x & 0x00ff0000) >> 8)  |
           ((x & 0xff000000) >> 24);
}

uint64_t bswap64(uint64_t x) {
    return ((x & 0x00000000000000ffULL) << 56) |
           ((x & 0x000000000000ff00ULL) << 40) |
           ((x & 0x0000000000ff0000ULL) << 24) |
           ((x & 0x00000000ff000000ULL) << 8)  |
           ((x & 0x000000ff00000000ULL) >> 8)  |
           ((x & 0x0000ff0000000000ULL) >> 24) |
           ((x & 0x00ff000000000000ULL) >> 40) |
           ((x & 0xff00000000000000ULL) >> 56);
}

// 按照要求使用指定的 align_up 寫法
// 所有資料和 Token 必須對齊 4-byte 
static inline const void* align_up(const void* ptr, size_t align) {
    return (const void*)(((uintptr_t)ptr + align - 1) & ~(align - 1));
}

// 計算字串長度
// 在 C 裡面不能用 .length() 因為 C 語言裡面 string 並不是一個 Object 
// 他只是一個以 \0 結尾的 character array
// C 語言沒有 Class (Object)
static size_t strlen(const char *s) {
    size_t len = 0;
    // \0 = false
    while (*s++) len++;
    return len;
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

// 在字串 s 中尋找第一個出現 char c 的地方
// static function 代表只有這個檔案的程式碼可以呼叫這個 function
static char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return 0;
}

// 當在尋找某個節點的時候，但是目前碰到的是無關的節點，這時候需要跳過他以及其他所有子節點
// eg. 跳過整個 soc 的子樹
static const uint32_t* skip_node(const uint32_t* p) {
    int depth = 1;
    while (depth > 0) {
        uint32_t token = bswap32(*p++);
        if (token == FDT_BEGIN_NODE) {
            p = (const uint32_t*)align_up((const char*)p + strlen((const char*)p) + 1, 4);
            depth++;
        } else if (token == FDT_END_NODE) {
            depth--;
        } else if (token == FDT_PROP) {
            uint32_t len = bswap32(*p++);
            p++;
            p = (const uint32_t*)align_up((const char*)p + len, 4);
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            break;
        }
    }
    return p;
}

// 把字串路徑 轉換成 memory offset
int fdt_path_offset(const void* fdt, const char* path) { // eg. /soc/serial@10000000
    struct fdt_header* h = (struct fdt_header*)fdt;
    if (bswap32(h->magic) != 0xd00dfeed) return -1;

    // off_struct = fdt 裡面 structure block 的起始位址
    uint32_t off_struct = bswap32(h->off_dt_struct);
    int current_offset = off_struct;

    if (path[0] != '/') return -1;
    if (path[1] == '\0') return current_offset;

    // a char = 1 byte
    const char* current_path = path + 1; // 略過第一個 '/'
    while (*current_path) {
        // 從目前剩下的字串中 尋找第一個斜線 若找得到 回傳指標位址
        const char* next_slash = strchr(current_path, '/'); 
        // 計算從目前位置到下一個斜線之間的字元長度
        int seg_len = next_slash ? (next_slash - current_path) : (int)strlen(current_path);

        // p 是一個指向記憶體位址的 4-byte 指標 他指向 FDT 檔案中某個具體節點的開頭
        // 因為 FDT 裡面規定 所有 TOKEN 和資料 都是以 4 bytes 為一個單位    
        const uint32_t* p = (const uint32_t*)((const char*)fdt + current_offset);
        // 檢查目前位置是否為一個節點的開頭 並將指標移動到下一格
        // FDT_BEGIN_NODE 是一種 TOKEN 代表這是一個 Node 的起始點
        if (bswap32(*p++) != FDT_BEGIN_NODE) return -1;
        // 跳過節點名稱字串 並將指標對齊到下一個 4-byte 邊界
        // eg. "uart\0" -> +8
        p = (const uint32_t*)align_up((const char*)p + strlen((const char*)p) + 1, 4);

        int found = 0;
        while (1) {
            // token (eg. 有可能是 begin_node / prop_node)
            uint32_t token = bswap32(*p++);
            if (token == FDT_BEGIN_NODE) {
                // node offset
                int node_offset = (const char*)(p - 1) - (const char*)fdt;
                // node name 
                const char* node_name = (const char*)p;
                
                // eg. serial@10000000
                // 為了區分同類型的硬體：如果一個 SoC 有多個相同類型的控制器 (eg. 4 個 UART)
                // 他們的節點名稱通常都叫做 serial 為了在同一個層級中區分它們 
                // Device Tree 規定名稱後方要加上 @ 以及該硬體的起始實體位址
                
                // 比對成功 下面這邊就是目前看到的 node 就是使用者要找的
                if (strncmp(node_name, current_path, seg_len) == 0 && // 對比 node_name 跟 current_path 這兩個 string 前 seg_len 個字元一不一樣
                    (node_name[seg_len] == '\0' || node_name[seg_len] == '@')) {
                    current_offset = node_offset;
                    found = 1;
                    break;
                }
                // 比對失敗 當目前的節點名稱不是我們要找的 需要跳過整個節點以及其內容
                p = (const uint32_t*)align_up(node_name + strlen(node_name) + 1, 4);
                p = skip_node(p);
            } else if (token == FDT_PROP) {
                // 在 FDT_PROP Token 後的第一個 32-bit 值就是這份 property 資料的長度
                // len 代表這個 property 佔用了多少個 bytes
                uint32_t len = bswap32(*p++);
                // 在 len 之後 接著的是一個 32-bit 的偏移量，Name offset 
                // 這個屬性名稱在字串表 string table 裡面的位置
                p++;
                // 跳過 Property 裡面的資料 並對齊到下一個 32-bits (4-bytes) 的邊界
                p = (const uint32_t*)align_up((const char*)p + len, 4);
            } else if (token == FDT_NOP) { // 佔位符號
                continue;
            } else if (token == FDT_END_NODE || token == FDT_END) {
                break;
            }
        }

        if (!found) return -1;
        // 跳過已經比對成功的這段節點名稱
        // eg. current_path 原本是 "soc/uart"
        // seg_len = 3 ("soc")
        // 執行後 指向了 "soc/uart" 的 /
        current_path += seg_len;
        if (*current_path == '/') current_path++;
    }
    return current_offset;
}

// get the specific property inside a node
// eg. fdt_getprop(fdt_ptr, offset, "reg", &len)
// 最後一個是 length pointer 主要是告訴 user 你要找的這份 property 的內容 佔用了多少個 byte
const void* fdt_getprop(const void* fdt, int nodeoffset, const char* name, int* lenp) {
    struct fdt_header * h = (struct fdt_header *)fdt;
    // check magic number 
    if (bswap32(h -> magic) != 0xd00dfeed) {
        return (void *)0;
    }

    // string table offset, in device tree, all property name are stored inside a string table
    uint32_t stroffset = bswap32(h -> off_dt_strings);
    // get the node address
    const uint32_t * p = (const uint32_t *)((const char *)fdt + nodeoffset);

    if (bswap32(*p++) != FDT_BEGIN_NODE) {
        return (void *)0;
    }
    
    p = (const uint32_t *)align_up((const char *)p + strlen((const char *)p) + 1, 4);

    while (1) {
        uint32_t token = bswap32(*p++);
        
        if (token == FDT_PROP) {
            uint32_t len = bswap32(*p++);
            uint32_t nameoff = bswap32(*p++);
            const char* prop_name = (const char*)fdt + stroffset + nameoff;
            
            if (strcmp(prop_name, name) == 0) {
                if (lenp) *lenp = len;
                return (const void*)p;
            }
            p = (const uint32_t*)align_up((const char*)p + len, 4);
        } else if (token == FDT_NOP) {
            continue;
        } else {
            break;
        }
    }
    return (void *)0;
}
