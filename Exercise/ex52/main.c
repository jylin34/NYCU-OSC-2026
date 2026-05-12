extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);
extern void video_init();
extern void video_bmp_display(unsigned int* bmp_image, int width, int height);

#define TIME_FREQ 10000000
int usleep(unsigned int usec) {
    unsigned long start, now;
    
    // 取得當前的硬體時間 (ticks)
    asm volatile("rdtime %0" : "=r"(start));
    
    // 計算目標時間：1us 對應 10 個 tick (10,000,000 / 1,000,000)
    unsigned long ticks = (unsigned long)usec * 10;
    
    do {
        asm volatile("rdtime %0" : "=r"(now));
    } while ((now - start) < ticks);
    
    return 0; // 補上 return 0 解決編譯警告
}

void display_video() {
#include "bird.h"
    while (1) {
        for (int f = 0; f < FRAME_COUNT; f++) {
            unsigned int* frame = (frames + (f * FRAME_WIDTH * FRAME_HEIGHT));
            video_bmp_display(frame, FRAME_WIDTH, FRAME_HEIGHT);
            usleep(50000);
        }
    }
}

void start_kernel() {
    uart_puts("\nStarting kernel ...\n");
    // TODO: Initialize the QEMU frame buffer device
    video_init();
    display_video();
}
