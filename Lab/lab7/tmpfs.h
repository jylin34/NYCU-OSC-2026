#ifndef TMPFS_H
#define TMPFS_H

#include "vfs.h"
#include <stddef.h>

struct tmpfs_inode {
    char name[26];      // file name
    int is_dir;         // 0: file, 1: directory
    
    // if it is file
    void* data;
    size_t size;        // file size
    
    // if it is directory
    struct vnode* entries[16];
    int entry_count;
    
    // Lab 7 Basic Exercise 3: 為支援 ".." 必須加入 parent 指標
    struct vnode* parent; 
};

int tmpfs_setup_mount(struct filesystem* fs, struct mount* mount);
int tmpfs_lookup(struct vnode *dir_node, struct vnode **target, const char *component_name);
int tmpfs_create(struct vnode *dir_node, struct vnode** target, const char *component_name);
int tmpfs_mkdir(struct vnode *dir_node, struct vnode** target, const char *component_name);
int tmpfs_open(struct vnode *file_node, struct file** target);
int tmpfs_close(struct file *file);
int tmpfs_read(struct file *file, void *buf, size_t len);
int tmpfs_write(struct file *file, const void *buf, size_t len);

// 如果你想保持封裝，你可以選擇只公開這一行，讓外部檔案不需要直接存取 inode 內部
// int tmpfs_is_dir(struct vnode *node);

#endif /* TMPFS_H */