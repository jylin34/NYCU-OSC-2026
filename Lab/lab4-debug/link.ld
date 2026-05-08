/* 指定輸出的二進位架構為 riscv */
OUTPUT_ARCH(riscv) 

/* 定義程式的入口點為 _start */
ENTRY(_start)

/* 定義記憶體的佈局 */
SECTIONS
{
    /* 在 riscv 中 0x80000000 通常是 DRAM 的起點 */
    /* 通常會先運行 OpenSBI 韌體 他會佔用一小部分的空間 */
    /* 等 OpenSBI 初始化完成後，他會跳轉到 0x80200000 來啟動 Kernel */
    /* QEMU: 0x80200000 / OrangePi: 0x00200000 */
    . = 0x00200000; /* 2MB */
    /* 如果要 load 的話改成 0x20000000 */
    _start = .;
    .text : { *(.text.boot) *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : {
        __bss_start = .; /* 把 . 目前指向的位址 給 __bss_start */
        *(.sbss) *(.bss) /* * 代表把所有輸入檔案裡的 .sbss 和 .bss 區段的資料 通通收集起來放在這個位址 */
        __bss_stop = .; 
    }

    . = ALIGN(8); 
    _image_end = .; /* _image_end 指向目前的位址 也就是 kernel image 的結尾 不用複製 stack */

    /* 這個函式會計算出大於或等於目前位址 且可以被 16 整除的最小位址 */
    . = ALIGN(16); /* RISC-V 規定 stack pointer 必須保持和 16-bytes 對齊 */

    /* 留給 Stack 的 16KB 空間 */
    . = . + 0x4000;

    /* _end 指向整個 kernel image + 16 KB 的最終的那個端點 */
    /* 也就是 stack 開始的位置 (向下增長) */
    _end = .;
}
