/*
Lab 7: Virtual File System
Bacis Exercise 4 - /ramfs - 15%
*/
#include "vfs.h"
#include <stddef.h>
#include <stdint.h>

extern void *cpio_base;
extern void *allocate(size_t size);

static struct file_operations ramfs_file_ops;
static struct vnode_operations ramfs_vnode_ops;

// ============= Tool Function =============
static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

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

static int align(int n, int byte) {
    return (n + byte - 1) & ~(byte - 1);
}

static char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
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
// =========================================

struct ramfs_inode {
    char name[32];
    int is_dir;        // 0: 檔案, 1: 目錄
    void* data;        // 指向檔案內容的指標
    size_t size;       // 檔案大小
    struct vnode* entries[16]; // 如果是目錄，存放子節點
    int entry_count;
};

int ramfs_create(struct vnode *dir_node, struct vnode** target, const char *component_name) {
    return -1; 
}

int ramfs_mkdir(struct vnode *dir_node, struct vnode** target, const char *component_name) {
    return -1; 
}

int ramfs_write(struct file *file, const void *buf, size_t len) {
    return -1; 
}

int ramfs_setup_mount(struct filesystem* fs, struct mount* mount) {
    // 1. 建立 ramfs 的根目錄 vnode 與內部 inode
    struct vnode* root_vnode = allocate(sizeof(struct vnode));
    struct ramfs_inode* root_inode = allocate(sizeof(struct ramfs_inode));

    root_vnode -> mount = mount;
    root_vnode -> v_ops = &ramfs_vnode_ops;
    root_vnode -> f_ops = &ramfs_file_ops;
    root_vnode -> internal = root_inode;
    root_vnode -> parent = NULL; // 會由外部的 vfs_mount 自動指派母目錄

    root_inode -> is_dir = 1;
    root_inode -> entry_count = 0;
    root_inode -> name[0] = '\0';

    mount->root = root_vnode;

    // 如果系統沒有載入 initramfs，就直接留著空的唯讀根目錄
    if (!cpio_base) return 0; 

    // 2. 完美的複製你 exec() 內部的 CPIO 走位公式
    const char *ptr = (const char *)cpio_base;
    while (1) {
        const struct cpio_t *header = (const struct cpio_t *)ptr;

        // 簡單的魔術數字安全驗證
        if (header->magic[0] != '0' || header->magic[1] != '7') {
            break; 
        }

        uint32_t namesize = (uint32_t)hextoi(header->namesize, 8);
        uint32_t filesize = (uint32_t)hextoi(header->filesize, 8);
        const char *cur_filename = ptr + sizeof(struct cpio_t);

        // 檢查 CPIO 結束標籤
        if (strcmp(cur_filename, "TRAILER!!!") == 0) {
            break;
        }

        uint32_t data_offset = (uint32_t)align(sizeof(struct cpio_t) + namesize, 4);
        void *file_data = (void *)(ptr + data_offset);

        // 3. 排除 CPIO 內部的點目錄，只將常規檔案塞進 ramfs 根目錄
        if (strcmp(cur_filename, ".") != 0 && strcmp(cur_filename, "..") != 0) {
            struct vnode* new_vnode = allocate(sizeof(struct vnode));
            struct ramfs_inode* new_inode = allocate(sizeof(struct ramfs_inode));

            new_inode -> is_dir = 0;
            new_inode -> size = filesize;
            
            // 關鍵：直接指向高位址映射後的唯讀 CPIO 記憶體，不佔用額外 Heap
            new_inode -> data = file_data; 
            new_inode -> entry_count = 0;
            
            // 安全拷貝檔名，防止超過你定義的 name[32] 邊界
            strncpy(new_inode -> name, cur_filename, 31);
            new_inode -> name[31] = '\0';

            new_vnode -> internal = new_inode;
            new_vnode -> v_ops = &ramfs_vnode_ops;
            new_vnode -> f_ops = &ramfs_file_ops;
            new_vnode -> mount = NULL;
            new_vnode -> parent = root_vnode;

            // 4. 強制塞入 ramfs 根目錄的 entries 陣列
            if (root_inode -> entry_count < 16) {
                root_inode -> entries[root_inode -> entry_count++] = new_vnode;
            }
        }

        // 使用跟你 exec() 一模一樣的 4 位元組對齊前進公式
        ptr += align(data_offset + filesize, 4);
    }

    return 0;
}

struct filesystem ramfs = {
    .name = "ramfs",
    .setup_mount = ramfs_setup_mount,
};

// 在當前的目錄節點裡，找到那個叫 component_name 的子節點。
int ramfs_lookup(struct vnode *dir_node, struct vnode **target, const char *component_name) {
    // 1. 轉型取得該目錄的 inode
    struct ramfs_inode* inode = (struct ramfs_inode*)dir_node -> internal;

    // 2. 確保這是個目錄
    if (!inode -> is_dir) return -1;

    // 3. 遍歷該目錄下的所有 entries
    for (int i = 0; i < inode -> entry_count; i++) {
        struct vnode* entry_vnode = inode -> entries[i];
        struct ramfs_inode* entry_inode = (struct ramfs_inode*)entry_vnode -> internal;

        // 4. 比對檔案名稱
        if (strcmp(entry_inode -> name, component_name) == 0) {
            *target = entry_vnode;
            return 0; // 找到了！
        }
    }
    return -1; // 找不到
}

int ramfs_open(struct vnode *file_node, struct file** target) {
    struct ramfs_inode* inode = (struct ramfs_inode*)file_node->internal;
    if (inode->is_dir) {
        return -1; 
    }
    return 0;
}

int ramfs_close(struct file *file) {
    return 0;
}

int ramfs_read(struct file *file, void *buf, size_t len) {
    struct ramfs_inode* inode = (struct ramfs_inode*)file -> vnode -> internal;
    
    if (file -> f_pos >= inode -> size)    return 0;

    // 邊界檢查：算出最多還能讀多少 (readable size)
    size_t readable_size = inode -> size - file -> f_pos;
    
    // 取 min(len, readable_size)
    size_t actual_read = (len < readable_size) ? len : readable_size;

    // 將檔案內容從你的記憶體檔案系統中，複製到使用者指定的緩衝區 (buf) 裡。
    memcpy(buf, (char*)inode -> data + file -> f_pos, actual_read);

    // 更新 f_pos (這會讓下次讀取從新的位置開始)
    file -> f_pos += actual_read;

    return (int)actual_read;
}

static struct file_operations ramfs_file_ops = {
    .open  = ramfs_open,
    .close = ramfs_close,
    .read  = ramfs_read,
    .write = ramfs_write,
};

static struct vnode_operations ramfs_vnode_ops = {
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir  = ramfs_mkdir,
};