#ifndef VFS_H
#define VFS_H

#include <stddef.h>

#define O_CREAT 00000100

// Virtual Node: A node in a VFS tree
struct vnode {
    struct mount* mount; // Whether a directory serves as a mount point
    struct vnode_operations* v_ops; // Function pointers mapping to the specific vnode operations supported by the underlying file system
    struct file_operations* f_ops; // Function pointers mapping to the specific file operations supported by the underlying file system
    void* internal; // inode
    struct vnode* parent;
};

// file handle
struct file {
    struct vnode* vnode; // Point to the vnode that represent this file
    size_t f_pos;  // RW position of this file handle, The position of the read/write cursor.
    struct file_operations* f_ops;
    int flags; // 記錄當初是用什麼權限開啟這個檔案的 eg. read only, write only...
    int refcount; // 記錄目前有幾個 file descriptor 參考這個 file
};

// vnode 跟 file system 之間的橋梁
struct mount {
    struct vnode* root; // 被掛載進來的那個檔案系統的「根節點」
    struct filesystem* fs; // 紀錄被掛載進來的這個是用哪一種 filesystem
};

struct filesystem {
    const char* name; // 這個 file system 的名稱
    int (*setup_mount)(struct filesystem* fs, struct mount* mount);
};

struct file_operations { // 儲存跟 file operation 相關的 function pointers
    int (*open)(struct vnode* file_node, struct file** target);
    int (*close)(struct file* file);
    int (*read)(struct file* file, void* buf, size_t len);
    int (*write)(struct file* file, const void* buf, size_t len);
    long (*lseek64)(struct file* file, long offset, int whence);
    int (*ioctl)(struct file *file, unsigned long request, unsigned long arg);
};

struct vnode_operations { // 儲存跟 vnode operation 相關的 function pointers
    int (*lookup)(struct vnode* dir_node, struct vnode** target,
                const char* component_name);
    int (*create)(struct vnode* dir_node, struct vnode** target,
                const char* component_name);
    int (*mkdir)(struct vnode* dir_node, struct vnode** target,
               const char* component_name);
};

#endif