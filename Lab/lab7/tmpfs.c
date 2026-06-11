/*
Lab 7: Virtual File System
Bacis Exercise 1 - Root File System - 15%
*/
#include "vfs.h"
#include <stddef.h>

// ============= Tool Function =============
static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}
// =========================================

extern struct vnode;
extern struct file;
extern struct mount;
extern struct filesystem;

static struct file_operations tmpfs_file_ops;
static struct vnode_operations tmpfs_vnode_ops;

// The internal representation of each filesystem’s vnode may differ
struct tmpfs_inode {
    // For tmpfs, you can assume that component name won’t excced 15 characters, 
    // and at most 16 entries for a directory. and at most 4096 bytes for a file.
    char name[26]; // file name
    int is_dir; // 0: file, 1: directory
    // if it is file
    void* data;
    size_t size; // file size
    // if it is directory
    struct vnode* entries[16];
    int entry_count;
};

// 
int tmpfs_setup_mount(struct filesystem* fs, struct mount* mount) {
    // Each mounted file system has its own root vnode. 
    // You should create the root vnode during the mount setup.
    struct vnode* root_vnode = allocate(sizeof(struct vnode));
    struct tmpfs_inode* root_inode = allocate(sizeof(struct tmpfs_inode));

    root_vnode -> mount = mount;
    root_vnode -> v_ops = &tmpfs_vnode_ops;
    root_vnode -> f_ops = &tmpfs_file_ops;
    root_vnode -> internal = root_inode;
    root_vnode -> parent = NULL;
    
    // The internal representation of each filesystem’s vnode may differ, 
    // you can use vnode.internal to point to it.
    
    root_inode -> is_dir = 1;
    root_inode -> entry_count = 0;
    root_inode -> name[0] = '\0';

    mount -> root = root_vnode;
    return 0;
}

struct filesystem tmpfs = {
    .name = "tmpfs",
    .setup_mount = tmpfs_setup_mount,
};

// 在當前的目錄 dir_node 節點裡，找到那個叫 component_name 的子節點, 然後把 target 指向那個檔案的 vnode。
int tmpfs_lookup(struct vnode *dir_node, struct vnode **target, const char *component_name) {
    // 1. 轉型取得該目錄的 inode
    struct tmpfs_inode* inode = (struct tmpfs_inode*)dir_node -> internal;

    // 2. 確保這是個目錄
    if (!inode -> is_dir) return -1;

    // 3. 遍歷該目錄下的所有 entries
    for (int i = 0; i < inode -> entry_count; i++) {
        struct vnode* entry_vnode = inode -> entries[i];
        struct tmpfs_inode* entry_inode = (struct tmpfs_inode*)entry_vnode -> internal;

        // 4. 比對檔案名稱
        if (strcmp(entry_inode -> name, component_name) == 0) {
            *target = entry_vnode;
            return 0; // 找到了！
        }
    }
    return -1; // 找不到
}

// 在 dir_node 底下 create 一個叫做 component_name 的檔案，然後讓 target 指向這個檔案的 vnode
int tmpfs_create(struct vnode *dir_node, struct vnode** target, const char *component_name) {
    struct tmpfs_inode* dir_inode = (struct tmpfs_inode*)dir_node -> internal;

    // 1. 檢查目錄容量 (最多 16 個 entry)
    if (dir_inode -> entry_count >= 16) return -1;

    // 2. 檢查檔案是否已存在 (題目要求：fail if file exist)
    for (int i = 0; i < dir_inode -> entry_count; i++) {
        struct tmpfs_inode* entry_inode = (struct tmpfs_inode*)dir_inode -> entries[i] -> internal;
        if (strcmp(entry_inode -> name, component_name) == 0) return -1;
    }

    // 3. 分配並初始化新的 vnode 與 inode
    struct vnode* new_vnode = allocate(sizeof(struct vnode));
    struct tmpfs_inode* new_inode = allocate(sizeof(struct tmpfs_inode));

    new_inode -> is_dir = 0; // 這是普通檔案
    new_inode -> size = 0;
    new_inode -> data = allocate(4096); // 預分配空間
    strcpy(new_inode -> name, component_name);

    new_vnode -> internal = new_inode;
    new_vnode -> v_ops = &tmpfs_vnode_ops; // 記得要綁定 Ops
    new_vnode -> f_ops = &tmpfs_file_ops;
    new_vnode -> mount = NULL;
    new_vnode -> parent = dir_node;

    // 4. 將新節點掛入父目錄的 entries
    dir_inode -> entries[dir_inode -> entry_count++] = new_vnode;

    // 5. 回傳結果
    *target = new_vnode;
    return 0;
}

// create a directory name called component_name under dir_node directory,
// then let target point to that directory
int tmpfs_mkdir(struct vnode *dir_node, struct vnode** target, const char *component_name) {
    struct tmpfs_inode* dir_inode = (struct tmpfs_inode*)dir_node -> internal;

    // 1. 容量檢查
    if (dir_inode->entry_count >= 16) return -1;

    // 2. 名稱衝突檢查
    for (int i = 0; i < dir_inode->entry_count; i++) {
        struct tmpfs_inode* entry = (struct tmpfs_inode*)dir_inode->entries[i]->internal;
        if (strcmp(entry->name, component_name) == 0) return -1;
    }

    // 3. 分配新節點
    struct vnode* new_vnode = allocate(sizeof(struct vnode));
    struct tmpfs_inode* new_inode = allocate(sizeof(struct tmpfs_inode));

    // 4. 初始化目錄屬性
    new_inode->is_dir = 1;         // !!! 關鍵：標記為目錄
    new_inode->entry_count = 0;    // 初始化子目錄數量為 0
    strcpy(new_inode->name, component_name);
    
    // 綁定操作介面
    new_vnode->internal = new_inode;
    new_vnode->v_ops = &tmpfs_vnode_ops; // 確保目錄也有 lookup/mkdir 能力
    new_vnode->f_ops = &tmpfs_file_ops;
    new_vnode->parent = dir_node;
    new_vnode->mount = NULL;

    // 5. 掛入父目錄
    dir_inode->entries[dir_inode->entry_count++] = new_vnode;

    *target = new_vnode;
    return 0;
}

// file_node 是想要打開的檔案所在的 vnode, 
int tmpfs_open(struct vnode *file_node, struct file** target) {
    struct tmpfs_inode* inode = (struct tmpfs_inode*)file_node -> internal;
    if (inode->is_dir)  return -1;
    return 0;
}

int tmpfs_close(struct file *file) {
    return 0;
}

// 從 file 把 len 長度的資料 丟進 buf
int tmpfs_read(struct file *file, void *buf, size_t len) {
    struct tmpfs_inode* inode = (struct tmpfs_inode*)file -> vnode -> internal;
    
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

// 從 but 裡面讀取 Len 長度的東西 丟進 file 
int tmpfs_write(struct file *file, const void *buf, size_t len) {
    struct tmpfs_inode* inode = (struct tmpfs_inode*)file -> vnode -> internal;
    
    // 1. 記憶體擴展檢查 (如果 f_pos + len 超過目前的容量)
    // For tmpfs, you can assume that at most 4096 bytes for a file.
    if (file -> f_pos + len > 4096) len = 4096 - file -> f_pos;

    if (len <= 0) return 0;

    // 2. 執行寫入 (從 buf 複製到 inode->data + f_pos)
    memcpy((char*)inode -> data + file -> f_pos, buf, len);

    // 3. 更新 f_pos 與 size
    file -> f_pos += len;
    
    // 如果寫入後發現檔案變大了，更新 size
    if (file -> f_pos > inode -> size)  inode -> size = file -> f_pos;

    return (int)len;
}

static struct file_operations tmpfs_file_ops = {
    .open  = tmpfs_open,
    .close = tmpfs_close,
    .read  = tmpfs_read,
    .write = tmpfs_write,
};

static struct vnode_operations tmpfs_vnode_ops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir  = tmpfs_mkdir,
};