# start.S 是程式執行的第一段組語
# 主要是為了執行 C 語言的環境做準備
.section ".text.boot"
.global _start
_start:
    # 把 __bss_start address load 給 a3
    la a3, __bss_start
    la a4, __bss_stop
    ble a4, a3, clear_bss_done
clear_bss:
    sd zero, (a3)
    # 因為是 RISC-V 64 位元 所以以 64 bit 為單位 也就是 8 bytes 
    add a3, a3, 8 # 以 8 bytes 為單位 逐一填上 0
    blt a3, a4, clear_bss
clear_bss_done:
    # RISC-V 啟動的時候 OpenSBI 會把 fdt 的 base address 放在 a1 register
    # load fdt base address(a1) to t0
    la t0, fdt_ptr
    sd a1, 0(t0)

    # 把 stack pointer 指向 _end (stack 開始的位置)
    la sp, _end

    # 跳轉到 start_kernel
    # 傳統的 jump 是預期跳轉之後 最後會回來繼續執行下面的程式碼
    # tail 是直接跳轉過去 不會再回來
    tail start_kernel
