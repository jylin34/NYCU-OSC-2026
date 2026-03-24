#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

// Flattened Device Tree Header 
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

// FDT 的格式規定資料必須 Big-Endian
// RISC-V 或多數的處理器 (x86 / ARM) 都預設使用 Little-Endian
// 所以這邊為了讓 RISC-V 正確讀到數值 要反轉一下位置
// static: 這個 function 僅限於這個檔案可以使用
// inline: 告訴編譯器可以直接將這個函式嵌入到程式裡面呼叫他的位置
// 用來提高效率 不需用每次都跳轉到這個函式的位置
static inline uint32_t bswap32(uint32_t x) {
    return __builtin_bswap32(x);
    // __builtin 是 Compiler 內建的函式
}

static inline uint64_t bswap64(uint64_t x) {
    return __builtin_bswap64(x);
}

// DTB 規定所有資料必須對齊 4-byte 
// eg. "soc" = 3 bytes + '\0' = 4 bytes
// eg. "uart" = 4 bytes + '\0' + '\0\0\0' = 8 bytes
static inline const void* align_up(const void* ptr, size_t align) {
    return (const void*)(((uintptr_t)ptr + align - 1) & ~(align - 1));
}

// 輔助函式：跳過整個節點（包含其所有屬性與子節點）
static const uint32_t* skip_node(const uint32_t* p) {
    int depth = 1;
    while (depth > 0) {
        uint32_t token = bswap32(*p++);
        if (token == FDT_BEGIN_NODE) {
            // 跳過名稱
            p = (const uint32_t*)align_up((const char*)p + strlen((const char*)p) + 1, 4);
            depth++;
        } else if (token == FDT_END_NODE) {
            depth--;
        } else if (token == FDT_PROP) {
            uint32_t len = bswap32(*p++);
            p++; // 跳過 nameoff
            p = (const uint32_t*)align_up((const char*)p + len, 4);
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            break;
        }
    }
    return p;
}

// 根據路徑找節點位置
// eg. 給 /cpus/cpu@0 他會去解析 dtb 找到該節點在記憶體中的 offset
// dtb base address 會存在 a1 register + offset 就是這個硬體的 metadata 位置
int fdt_path_offset(const void* fdt, const char* path) {
    struct fdt_header* h = (struct fdt_header*)fdt;
    if (bswap32(h->magic) != 0xd00dfeed) return -1;

    uint32_t off_struct = bswap32(h->off_dt_struct);
    int current_offset = off_struct;

    if (path[0] != '/') return -1;
    if (path[1] == '\0') return current_offset; // 根節點 "/"

    const char* current_path = path + 1;
    while (*current_path) {
        // 取得當前層級的路徑段落 (例如 /soc/serial -> 第一段是 "soc")
        const char* next_slash = strchr(current_path, '/');
        int seg_len = next_slash ? (next_slash - current_path) : strlen(current_path);

        const uint32_t* p = (const uint32_t*)((const char*)fdt + current_offset);
        if (bswap32(*p++) != FDT_BEGIN_NODE) return -1;
        p = (const uint32_t*)align_up((const char*)p + strlen((const char*)p) + 1, 4);

        int found = 0;
        while (1) {
            uint32_t token = bswap32(*p++);
            if (token == FDT_BEGIN_NODE) {
                int node_offset = (const char*)(p - 1) - (const char*)fdt;
                const char* node_name = (const char*)p;
                
                // 比對段落名稱 (需考慮 @ 位址後綴)
                if (strncmp(node_name, current_path, seg_len) == 0 && 
                    (node_name[seg_len] == '\0' || node_name[seg_len] == '@')) {
                    current_offset = node_offset;
                    found = 1;
                    break;
                }
                // 名稱不符，跳過這個子節點及其內容
                p = (const uint32_t*)align_up(node_name + strlen(node_name) + 1, 4);
                p = skip_node(p);
            } else if (token == FDT_PROP) {
                uint32_t len = bswap32(*p++);
                p++; // 跳過 nameoff
                p = (const uint32_t*)align_up((const char*)p + len, 4);
            } else if (token == FDT_NOP) {
                continue;
            } else if (token == FDT_END_NODE || token == FDT_END) {
                break; // 找完了當前層級還是沒找到
            }
        }

        if (!found) return -1;
        current_path += seg_len;
        if (*current_path == '/') current_path++;
    }
    return current_offset;
}

// 這裡是在找 有什麼
// 在特定的節點裡面 找到指定的屬性
// const void* 一個指向記憶體的指標
// 如果找到了 會回傳 DTB 內部的那段指標 如果找不到回傳 NULL
const void* fdt_getprop(const void* fdt, // DTB base address
                        int nodeoffset,  // node offset
                        const char* name,// property name
                        int* lenp) {     // 輸出這個屬性資料的長度到這個 pointer 讓呼叫的地方知道這個 property 多大 
    // Check fdt header Magic number (check if this is a devicetree format)
    struct fdt_header * h = (struct fdt_header *)fdt;
    if (bswap32(h -> magic) != 0xd00dfeed) {
        return NULL;
    }

    // Get fdt string offset 
    uint32_t stroffset = bswap32(h -> off_dt_strings);

    // Start reading from nodeoffset
    const uint32_t * p = (const uint32_t *)((const char *)fdt + nodeoffset);

    // Check if the first Token is FDT_BEGIN_NODE
    if (bswap32(*p++) != FDT_BEGIN_NODE) {
        return NULL;
    }
    
    // 跳過節點名稱字串
    p = (const uint32_t *)align_up((const char *)p + strlen((const char *)p) + 1, 4);

    // 開始搜尋屬性
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
            // 跳過屬性資料
            p = (const uint32_t*)align_up((const char*)p + len, 4);
        } else if (token == FDT_NOP) {
            continue;
        } else {
            // 遇到新的節點或結束，說明屬性區結束了
            break;
        }
    }
    return NULL;
}

int main() {
    /* Prepare the device tree blob */
    FILE* fp = fopen("qemu.dtb", "rb");
    if (!fp) {
        perror("fopen");
        return EXIT_FAILURE;
    }
    // 計算檔案大小
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    void* fdt = malloc(sz);
    fseek(fp, 0, SEEK_SET);
    // 把檔案內容丟進剛剛 malloc 開好的空間裡面
    if (fread(fdt, 1, sz, fp) != sz) {
        fprintf(stderr, "Failed to read the device tree blob\n");
        free(fdt);
        fclose(fp);
        return EXIT_FAILURE;
    }
    fclose(fp);

    /* Find the node offset */
    int offset = fdt_path_offset(fdt, "/cpus/cpu@0/interrupt-controller");
    if (offset < 0) {
        fprintf(stderr, "fdt_path_offset\n");
        free(fdt);
        return EXIT_FAILURE;
    }

    /* Get the node property */
    int len;
    const void* prop = fdt_getprop(fdt, offset, "compatible", &len);
    if (!prop) {
        fprintf(stderr, "fdt_getprop\n");
        free(fdt);
        return EXIT_FAILURE;
    }
    printf("compatible: %.*s\n", len, (const char*)prop);

    offset = fdt_path_offset(fdt, "/memory");
    prop = fdt_getprop(fdt, offset, "reg", &len);
    const uint64_t* reg = (const uint64_t*)prop;
    printf("memory: base=0x%lx size=0x%lx\n", bswap64(reg[0]), bswap64(reg[1]));

    offset = fdt_path_offset(fdt, "/chosen");
    prop = fdt_getprop(fdt, offset, "linux,initrd-start", &len);
    const uint64_t* initrd_start = (const uint64_t*)prop;
    printf("initrd-start: 0x%lx\n", bswap64(initrd_start[0]));

    free(fdt);
    return 0;
}
