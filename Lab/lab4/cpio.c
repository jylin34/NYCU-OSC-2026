#include <stdint.h>
#include <stddef.h>

// 宣告由 main.c 或其他硬體抽象層提供的 UART 輸出函數
extern void uart_puts(const char* s);
extern void uart_putc(char c);
extern void uart_hex(unsigned long h);

// https://man.freebsd.org/cgi/man.cgi?query=cpio&sektion=5
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

/**
 * @brief Convert a hexadecimal string to integer
 */
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

/**
 * @brief Align a number to the nearest multiple of a given number
 */
static int align(int n, int byte) {
    return (n + byte - 1) & ~(byte - 1);
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

// List files in the initrd
void initrd_list(const void* rd) {
    const char* ptr = (const char*)rd; // cpio base address
   
    while(1) {        
        const struct cpio_t* header = (const struct cpio_t*)ptr;
        
        if (strncmp(header->magic, "070701", 6) != 0) {
            uart_puts("Error: Invalid Magic Number\n");
            return;
        }

        uint32_t namesize = hextoi(header->namesize, 8);
        uint32_t filesize = hextoi(header->filesize, 8);
        const char* filename = ptr + sizeof(struct cpio_t);

        if (!strcmp(filename, "TRAILER!!!")) break;
        // 如果有一個檔名叫做  TRAILER!!! 的檔案 那到這邊就會 break 掉

        // 印出檔案資訊
        uart_puts(filename);
        uart_puts(" (size: ");
        uart_hex(filesize);
        uart_puts(")\n");

        uint32_t data_offset = align(sizeof(struct cpio_t) + namesize, 4);
        ptr += align(data_offset + filesize, 4);
    }
}

// Display file contents in the initrd
void initrd_cat(const void* rd, const char* filename) {
    const char* ptr = (const char*)rd; // cpio base address

    while(1) {        
        const struct cpio_t* header = (const struct cpio_t*)ptr;
        
        if (strncmp(header->magic, "070701", 6) != 0) {
            uart_puts("Error: Invalid Magic Number\n");
            return;
        }

        uint32_t namesize = hextoi(header->namesize, 8);
        uint32_t filesize = hextoi(header->filesize, 8);
        const char* cur_filename = ptr + sizeof(struct cpio_t);

        if (!strcmp(cur_filename, "TRAILER!!!")) {
            uart_puts("cat: ");
            uart_puts(filename);
            uart_puts(": No such file or directory\n");
            break;
        }

        if (!strcmp(cur_filename, filename)) {
            uint32_t data_offset = align(sizeof(struct cpio_t) + namesize, 4);
            const char* data_ptr = ptr + data_offset;
            
            // 逐字元印出內容
            for (uint32_t i = 0; i < filesize; i++) {
                uart_putc(data_ptr[i]);
            }
            uart_putc('\n');
            return;
        }

        uint32_t data_offset = align(sizeof(struct cpio_t) + namesize, 4);
        ptr += align(data_offset + filesize, 4);
    }
}
