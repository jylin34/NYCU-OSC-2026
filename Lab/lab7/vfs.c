/*
Lab 7: Virtual File System
*/
#include "vfs.h"
#include <stddef.h>
#include <stdint.h>

// rootfs 這個全域變數就是指向那個 / 節點的指標，它是整個檔案系統樹的頂端。
// 就是 / 這個 root 的 filesystem
struct mount* rootfs;
extern struct filesystem tmpfs;
extern struct filesystem ramfs;

// ============= Tool Function =============
extern void *allocate(size_t size);
extern void free(void *ptr);

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char *strtok(char *str, const char *delim) {
    static char *buffer = NULL;
    if (str != NULL) buffer = str;
    if (buffer == NULL) return NULL;

    char *start = buffer;
    while (*buffer != '\0') {
        int is_delim = 0;
        for (int i = 0; delim[i] != '\0'; i++) {
            if (*buffer == delim[i]) {
                is_delim = 1;
                break;
            }
        }
        if (is_delim) {
            *buffer = '\0';
            buffer++;
            return start;
        }
        buffer++;
    }
    
    if (start == buffer) return NULL;
    buffer = NULL;
    return start;
}

char *strrchr(const char *s, int c) {
    char *last = NULL;
    
    // 遍歷整個字串，直到遇到結尾 '\0'
    while (*s != '\0') {
        // 如果當前字元符合目標，記錄下該位址
        // 因為我們是從頭掃到尾，所以最後一次被記錄的位址就是「最後出現」的位址
        if (*s == (char)c) {
            last = (char *)s;
        }
        s++;
    }
    
    // 特別處理：如果目標字元是 '\0' (字串結尾)，它也算是最後一個出現的位置
    if ((char)c == '\0') {
        return (char *)s;
    }
    
    return last;
}

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

enum task_state {
    TASK_RUNNING,
    TASK_RUNNABLE,
    TASK_ZOMBIE
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
// =========================================

#define MAX_FILESYSTEMS 10
static struct filesystem* fs_list[MAX_FILESYSTEMS];
static int fs_count = 0;

int register_filesystem(struct filesystem* fs) {
    // register the file system to the kernel.
    // you can also initialize memory pool of the file system here.
    if (fs_count >= MAX_FILESYSTEMS) return -1;
    fs_list[fs_count++] = fs;
    return 0;
}

// create an regular file on underlying file system, should fail if file exist.
// 針對 pathname 建立一個檔案，然後讓 target 指向那個檔案的 vnode
int vfs_create(const char* pathname, struct vnode** target) {
    // 1. 分割路徑：取得父目錄路徑與新檔名
    // 例如："/home/test.txt" -> parent="/home", name="test.txt"
    char path_copy[128];
    strcpy(path_copy, pathname);
    
    // 找出最後一個 '/' 的位置來分割
    char* last_slash = strrchr(path_copy, '/');
    *last_slash = '\0'; // 將路徑切斷
    char* filename = last_slash + 1;

    // 2. 透過 vfs_lookup 找到父目錄的 vnode
    struct vnode* parent_vnode = NULL;
    if (vfs_lookup(path_copy, &parent_vnode) != 0) return -1;

    // 3. 呼叫底層 v_ops->create 建立檔案
    return parent_vnode -> v_ops -> create(parent_vnode, target, filename);
}

// 要以 flags 的權限去打開 pathname 這個檔案，然後把 target 指向這個檔案
int vfs_open(const char* pathname, int flags, struct file** target) {
    struct vnode* target_vnode = NULL;

    // 1. Lookup pathname
    int ret = vfs_lookup(pathname, &target_vnode);

    // 2. Create a new file handle for this vnode if found.
    if (ret != 0 && (flags & O_CREAT)) {
        ret = vfs_create(pathname, &target_vnode);
    }

    // fail (return -1)
    if (ret != 0)   return ret;
    
    // 3. Create a new file if O_CREAT is specified in flags and vnode not found
    // lookup error code shows if file exist or not or other error occurs
    struct file* f = allocate(sizeof(struct file));
    
    if (f == NULL)  return -1;

    f -> vnode = target_vnode;
    f -> f_pos = 0;
    f -> flags = flags;
    f -> f_ops = target_vnode -> f_ops;
    f -> refcount = 1; // 初始開啟時，計數為 1

    if (f -> f_ops == NULL) {
        // 先檢查 f_ops 是不是空的，避免下一行 f->f_ops->open 直接當機
        // uart_puts("[Kernel] vfs_open: WARNING - target_vnode->f_ops is NULL!\n");
    } else {
        if (f -> f_ops -> open != NULL) {
            int open_ret = f -> f_ops -> open(target_vnode, &f);
            if (open_ret != 0) {
                // free(f); 暫時保持註解或移除
                return open_ret;
            }
        }
    }

    *target = f;
    return 0; // success
}

int vfs_close(struct file* file) {
    if (file == NULL)   return -1;
    
    file->refcount--;
    if (file->refcount > 0) return 0; // 若還有其他人參考這個 file，不能釋放

    if (file -> f_ops -> close != NULL) {
        file -> f_ops -> close(file);
    }
    // 1. Release the file handle
    free(file); // 沒有人參考了，安全釋放避免 memory leak
    return 0;
}

// 從 but 裡面讀取 Len 長度的東西 丟進 file 
int vfs_write(struct file* file, const void* buf, size_t len) {
    // Return error code if an error occurs.
    if (file == NULL || buf == NULL) return -1;
    if (file -> f_ops -> write == NULL) return -1;
    // Return written size
    return file -> f_ops -> write(file, buf, len);
}

// 從 file 把 len 長度的資料 丟進 buf
int vfs_read(struct file* file, void* buf, size_t len) {
    if (file == NULL || buf == NULL) return -1;
    if (file -> f_ops -> read == NULL) return -1;

    int read_bytes = file ->f_ops -> read(file, buf, len);

    // Return read size or error code if an error occurs.
    return read_bytes;
}

// find the corresponding target vnode
// 把找到的檔案的 vnode 傳給 target
int vfs_lookup(const char* pathname, struct vnode** target) {
    if (strcmp(pathname, "/") == 0) {
        *target = rootfs -> root;
        return 0;
    }

    struct vnode* current_vnode = rootfs -> root;

    // 判斷是絕對路徑 or 相對路徑
    if (pathname[0] == '/') {
        current_vnode = rootfs -> root;
    } else {
        struct task_struct *curr = get_current();
        current_vnode = curr -> cwd; 
    }

    char path_copy[128];
    strcpy(path_copy, pathname);
    char* component = strtok(path_copy, "/");
    
    // 假設 path = /home/test.txt
    // 這邊就是透過 strtok 切分成 home, test.txt, 然後每一次去呼叫 tmpfs_lookup，找到對應的 vnode
    while (component != NULL) {
        // 如果切出來的 component 是空的 (例如開頭的 / 會切出空字串)
        // 或者是連續的雙斜線 //，我們就直接略過它，繼續切下一個！
        if (strcmp(component, "") == 0 || strcmp(component, ".") == 0) {
            component = strtok(NULL, "/");
            continue;
        }
        if (strcmp(component, "..") == 0) {
            if (current_vnode -> parent != NULL) {
                current_vnode = current_vnode -> parent;
            }
            component = strtok(NULL, "/");
            continue;
        }
        // 呼叫底層的 lookup 檢查目錄中是否有這個 component
        struct vnode* next_vnode = NULL;
        if (current_vnode -> v_ops -> lookup(current_vnode, &next_vnode, component) != 0) {
            return -1; // 找不到檔案/目錄
        }
        current_vnode = next_vnode;
        // ========== Lab7: Basic Exercise 2 - Multi-level VFS ==========
        // 如果這個 vnode 上面掛著另一個 filesystem，直接跳轉到該 filesystem 的 root
        if (current_vnode -> mount != NULL) {
            current_vnode = current_vnode -> mount -> root;
        }
        // ====================
        component = strtok(NULL, "/");
    }

    // 4. 找到目標
    *target = current_vnode;
    return 0;
}

// 在 target 這個 vnode 上面 mount 一個新的 filesystem
int vfs_mount(const char* target, const char* filesystem) {
    // 把一個 file system 的 root node, mount 到另一個 vnode 上面
    // 1. Find the corresponding filesystem
    struct filesystem* fs = NULL;
    for (int i = 0; i < fs_count; i ++) {
        if (strcmp(fs_list[i]->name, filesystem) == 0) {
            fs = fs_list[i];
            break;
        }
    }
    if (!fs)    return -1;

    // 2. Create mount struct
    struct mount* mt = allocate(sizeof(struct mount));
    mt -> fs = fs;

    // 3. call the corresponding file system setup_mount function
    if (fs -> setup_mount(fs, mt) != 0) {
        free(mt);
        return -1;
    }

    // 4. 
    if (strcmp(target, "/") == 0) {
        rootfs = mt;
        return 0;
    }
    // you should be able to mount a filesystem on any vnode. 
    else {
        struct vnode* target_vnode = NULL;
        if (vfs_lookup(target, &target_vnode) != 0) return -1;
        target_vnode -> mount = mt;
        mt -> root -> parent = target_vnode -> parent;
        return 0;
    }
}

// Create a directory on underlying file system, same as creating a regular file.
int vfs_mkdir(const char* pathname) {
    char path_copy[128];
    strcpy(path_copy, pathname);
    
    // 從右到左找到最後一個 / 的位址
    char* last_slash = strrchr(path_copy, '/');
    
    if (last_slash == NULL) return -1; // 不支援在根目錄下不帶父路徑的創建
    *last_slash = '\0';

    char* dirname = last_slash + 1;
    struct vnode* parent_vnode = NULL;

    // eg. pathname = "/dev/uart"
    //     last_slash      |
    //     path_copy = "/dev"

    if (strcmp(path_copy, "") == 0) {
        // 如果切完是空的，代表本來是 "/ramfs"，父目錄就是 "/"
        if (vfs_lookup("/", &parent_vnode) != 0) return -1;
    } else {
        // 否則，正常查尋父目錄
        if (vfs_lookup(path_copy, &parent_vnode) != 0) return -1;
    }
    // 呼叫底層 mkdir
    struct vnode* target = NULL;
    return parent_vnode -> v_ops -> mkdir(parent_vnode, &target, dirname);
}

// to write framebuffer again without reopen
long vfs_lseek64(struct file* file, long offset, int whence) {
    if (file == NULL || file -> f_ops -> lseek64 == NULL) return -1;
    return file -> f_ops -> lseek64(file, offset, whence);
}

// to query framebuffer info
int vfs_ioctl(struct file* file, unsigned long request, unsigned long arg) {
    if (file == NULL || file -> f_ops -> ioctl == NULL) return -1;
    return file -> f_ops -> ioctl(file, request, arg);
}

// ========== Lab7: Advance Exercise 1 ==========

extern char uart_getc(void);
extern void uart_putc(char c);

int uart_dev_open(struct vnode *file_node, struct file** target) {
    return 0;
}

int uart_dev_close(struct file *file) {
    return 0; 
}

int uart_dev_read(struct file *file, void *buf, size_t len) {
    char *c_buf = (char *)buf;
    for (size_t i = 0; i < len; i++) {
        c_buf[i] = uart_getc(); 
    }
    return (int)len;
}

int uart_dev_write(struct file *file, const void *buf, size_t len) {
    const char *c_buf = (const char *)buf;
    for (size_t i = 0; i < len; i++) {
        uart_putc(c_buf[i]); 
    }
    return (int)len;
}

// 建立 UART 專屬的 VFS 介面
static struct file_operations uart_dev_file_ops = {
    .open  = uart_dev_open,
    .close = uart_dev_close,
    .read  = uart_dev_read,
    .write = uart_dev_write,
};

// ==============================================

// ========== Lab7: Advance Exercise 2 ==========

#define FB_BASE   0x7f700000
#define FB_WIDTH  1920
#define FB_HEIGHT 1080
#define CACHE_BLOCK_SIZE 64
#define FB_BPP        4
#define FB_SIZE       (FB_WIDTH * FB_HEIGHT * FB_BPP)
#define PAGE_OFFSET 0xffffffc000000000UL

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

struct framebuffer_info {
    unsigned int width;
    unsigned int height;
    unsigned int bpp;  // bytes per pixel
};

int fb_dev_open(struct vnode *file_node, struct file** target) {
    return 0; // 隨時歡迎開啟
}

int fb_dev_close(struct file *file) {
    return 0; // 隨時歡迎關閉
}

int fb_dev_write(struct file *file, const void *buf, size_t len) {
    // 1. 邊界檢查：防止畫筆 f_pos 越界把核心其他地方戳爛
    if (file -> f_pos >= FB_SIZE) return 0;
    if (file -> f_pos + len > FB_SIZE) {
        len = FB_SIZE - file -> f_pos;
    }
    if (len <= 0) return 0;

    // 2. 計算實體螢幕高位址映射後的目標記憶體位址
    unsigned long target_addr = FB_BASE + PAGE_OFFSET + file -> f_pos;

    // 3. 執行寫入 
    // 註：因為你的 sys_write 內部在呼叫 vfs_write 之前已經幫你開啟了 SUM bit，
    // 所以這裡可以直接用 memcpy 安全地把 User 傳進來的像素顏色搬到螢幕上！
    memcpy((void *)target_addr, buf, len);

    // 4. 關鍵：立刻洗掉 CPU Data Cache，螢幕才會即時顯色
    flush_dcache((void *)target_addr, len);

    // 5. 更新前進畫筆位置
    file -> f_pos += len;

    return (int)len;
}

// to write framebuffer again without reopen
long fb_dev_lseek(struct file *file, long offset, int whence) {
    //  You only need to implement SEEK_SET (value 0).
    if (whence == 0) { // SEEK_SET: 從頭開始計算偏移量
        if (offset < 0 || offset > FB_SIZE) return -1; // 越界
        file -> f_pos = offset; // 移動畫筆
        return file -> f_pos;
    }
    return -1; // 本題只要求支援 SEEK_SET
}

// input/output control
// to query framebuffer info
int fb_dev_ioctl(struct file *file, unsigned long request, unsigned long arg) {
    if (request == 0) { // FB_IOCTL_GET_INFO
        // arg 是一個指向 User Space struct framebuffer_info 的指標
        struct framebuffer_info *user_fb = (struct framebuffer_info *)arg;

        unsigned long sstatus;
        asm volatile("csrr %0, sstatus" : "=r"(sstatus));
        asm volatile("csrs sstatus, %0" :: "r"(1UL << 18)); // 開啟 SUM

        user_fb -> width  = FB_WIDTH;
        user_fb -> height = FB_HEIGHT;
        user_fb -> bpp    = FB_BPP; 

        asm volatile("csrw sstatus, %0" :: "r"(sstatus)); // 還原 sstatus
        return 0;
    }
    return -1;
}

static struct file_operations fb_dev_file_ops = {
    .open   = fb_dev_open,   // 直接 return 0
    .close  = fb_dev_close,  // 直接 return 0
    .write  = fb_dev_write,  // ➔ 核心：計算 f_pos，直擊 FB_BASE 寫入像素並 flush dcache
    .lseek64  = fb_dev_lseek,  // ➔ 核心：支援 SEEK_SET 調整 f_pos 的畫筆位置
    .ioctl  = fb_dev_ioctl,  // ➔ 核心：把 width, height, bpp 餵給 User Space
};

// ==============================================

void vfs_init() {
    register_filesystem(&tmpfs);
    vfs_mount("/", "tmpfs"); // 將 tmpfs 這個檔案系統實作，掛載到了 VFS 的根目錄 (/) 上。
    rootfs -> root -> parent = rootfs -> root;

    // Lab7: Basic Exercise 4
    register_filesystem(&ramfs);
    vfs_mkdir("/ramfs");
    vfs_mount("/ramfs", "ramfs");

    // Lab7: Advance Exercise 1
    vfs_mkdir("/dev"); 
    struct vnode* uart_vnode = NULL;
    if (vfs_create("/dev/uart", &uart_vnode) == 0) {
        uart_vnode -> f_ops = &uart_dev_file_ops;
    }

    // Lab7: Advance Exercise 2
    struct vnode* fb_vnode = NULL;
    if (vfs_create("/dev/fb", &fb_vnode) == 0) {
        fb_vnode -> f_ops = &fb_dev_file_ops;
    }
}