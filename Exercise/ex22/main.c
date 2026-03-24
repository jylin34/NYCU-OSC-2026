#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

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
 *
 * @param s hexadecimal string
 * @param n length of the string
 * @return integer value
 */
static int hextoi(const char* s, int n) {
    int r = 0;
    while (n-- > 0) {
        r = r << 4;
        if (*s >= 'A')
            r += *s++ - 'A' + 10;
        else if (*s >= 0)
            r += *s++ - '0';
    }
    return r;
}

/**
 * @brief Align a number to the nearest multiple of a given number
 *
 * @param n number
 * @param byte alignment
 * @return aligned number
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

// List files in the initrd, you should list all file's size and name.
void initrd_list(const void* rd) {
    // TODO: Implement this function
    const char* ptr = (const char*)rd;
    uint32_t count = 0; // count number of files
   
    while(1) {        
        // check magic
        const struct cpio_t* header = (const struct cpio_t*)ptr;
        if (strncmp(header -> magic, "070701", 6)) {
            printf("Error: Invalid Magic Number");
            return;
        }
        // parsing namesize
        const uint32_t namesize = hextoi(header -> namesize, 8); // 8 bytes = 32 bits
        // parsing filesize
        const uint32_t filesize = hextoi(header -> filesize, 8);
        // get the actual filename 
        const char* filename = ptr + sizeof(struct cpio_t);
        // check if this is the end 
        if (!strcmp(filename, "TRAILER!!!")) {
            break;
        }
        count ++;
        // update ptr to next file
        // 要分兩次 align 不然有可能會錯
        const uint32_t data_offset = align(sizeof(struct cpio_t) + namesize, 4);
        ptr += align(data_offset + filesize, 4);
    }
    printf("Total %d files.\n", count);

    ptr = (const char*)rd;
    while(1) {        
        const struct cpio_t* header = (const struct cpio_t*)ptr;
        if (strncmp(header -> magic, "070701", 6)) {
            printf("Error: Invalid Magic Number");
            return;
        }
        const uint32_t namesize = hextoi(header -> namesize, 8); // 8 bytes = 32 bits
        const uint32_t filesize = hextoi(header -> filesize, 8);
        const char* filename = ptr + sizeof(struct cpio_t);
        if (!strcmp(filename, "TRAILER!!!")) {
            break;
        }
        printf("%-10u %s\n", filesize, filename);
        const uint32_t data_offset = align(sizeof(struct cpio_t) + namesize, 4);
        ptr += align(data_offset + filesize, 4);
    }

    return;
}

// Display file contents in the initrd, detect whether the file exists.
void initrd_cat(const void* rd, const char* filename) {
    // TODO: Implement this function
    const char* ptr = (const char*)rd;

    while(1) {        
        // check magic
        const struct cpio_t* header = (const struct cpio_t*)ptr;
        if (strncmp(header -> magic, "070701", 6)) {
            printf("Error: Invalid Magic Number");
            return;
        }
        // parsing namesize
        const uint32_t namesize = hextoi(header -> namesize, 8); // 8 bytes = 32 bits
        // parsing filesize
        const uint32_t filesize = hextoi(header -> filesize, 8);
        // get the actual filename 
        const char* cur_filename = ptr + sizeof(struct cpio_t);
        // check if this is the end 
        if (!strcmp(cur_filename, "TRAILER!!!")) {
            printf("cat: %s: No such file or directory\n", filename);
            break;
        }
        // check if current file is the one we want
        if (!strcmp(cur_filename, filename)) {
            uint32_t data_offset = align(sizeof(struct cpio_t) + namesize, 4);
            const char* data_ptr = ptr + data_offset;
            fwrite(data_ptr, 1, filesize, stdout);
            printf("\n");
            return;
        }
        // update ptr to next file
        // 要分兩次 align 不然有可能會錯
        const uint32_t data_offset = align(sizeof(struct cpio_t) + namesize, 4);
        ptr += align(data_offset + filesize, 4);
    }

    return;
}

int main() {
    /* Prepare the initial RAM disk */
    FILE* fp = fopen("initramfs.cpio", "rb");
    if (!fp) {
        perror("fopen");
        return EXIT_FAILURE;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    void* rd = malloc(sz);
    fseek(fp, 0, SEEK_SET);
    if (fread(rd, 1, sz, fp) != sz) {
        fprintf(stderr, "Failed to read initramfs.cpio\n");
        free(rd);
        fclose(fp);
        return EXIT_FAILURE;
    }
    fclose(fp);
    // 上面這一段程式碼基本上就是 去讀 .cpio 檔案跟大小
    // 然後再 heap 開一個一樣大小的空間 把 .cpio 塞進去

    initrd_list(rd);
    initrd_cat(rd, "osc.txt");
    initrd_cat(rd, "test.txt");

    free(rd);
    return 0;
}
