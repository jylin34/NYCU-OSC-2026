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
    # 把 trap handler 放進 stvec
    # stvec 是一個 csr register，裡面放的地址就是當發生 trap 的時候 U-Mode 跳到 S-Mode 時 CPU 會跳去執行的程式碼
    la a2, handle_exception
    csrw stvec, a2

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

.global handle_exception
.align 2
// 不論 u -> s 或是 s -> s 都有可能執行 handle_exception
handle_exception:
    /*
     * u-mode 進 trap 時：sscratch 事先放的是 kernel sp
     * 先交換 sp/sscratch，拿回 kernel sp 來存 context
     */
    // csrrw: 把 sscratch 跟 sp 的值互換
    // 在 u-mode: sscratch 會儲存 kernel stack 的位址, 如果是 U-mode 來的 trap，sp 會拿到 kernel stack，所以 sp != 0
    // 在 s-mode: sscratch 會儲存 0, 如果是 S-mode 來的 trap，sscratch 原本通常是 0，所以交換後 sp == 0
    csrrw sp, sscratch, sp // 把目前的 sp 存到 sscratch 同時把 sscratch 的舊值放進 sp
    // sp != 0 會直接跳去執行 save_context
    bnez sp, save_context
    
    // csrr: 把 sscratch 的東西讀到 sp 
    // 如果原本就在 s-mode 就會執行這一行
    csrr sp, sscratch

// 這段是 trap 進來之後，先把當下 CPU 的狀態完整保存起來，等之後 do_trap() 處理完，再可以把現場還原回去。
// 這段就是「把 trap 發生當下的 CPU 現場全部存進 kernel stack」，讓後面的 do_trap() 可以放心做事，做完之後再把現場還原回來。
save_context:
    // 在現在的 kernel stack 上，開一個空間，把目前 CPU 一般暫存器全部存起來
    // 因為 trap handler 會「破壞原本執行中的暫存器內容」，不先存起來就回不去原本現場。
    addi sp, sp, -8 * 35 // stack 向下長 所以是負的，8 * 35 是因為要存 35 個 64-bit 的暫存器
    // sd: store double word，把 register 的 64-bit 值存到記憶體裡
    sd ra,  8 *  0(sp) // 把 ra 暫存器的 64-bit 值，存到記憶體位址 sp + 0。
    sd gp,  8 *  2(sp)
    sd tp,  8 *  3(sp)
    sd t0,  8 *  4(sp)
    sd t1,  8 *  5(sp)
    sd t2,  8 *  6(sp)
    sd s0,  8 *  7(sp)
    sd s1,  8 *  8(sp)
    sd a0,  8 *  9(sp)
    sd a1,  8 * 10(sp)
    sd a2,  8 * 11(sp)
    sd a3,  8 * 12(sp)
    sd a4,  8 * 13(sp)
    sd a5,  8 * 14(sp)
    sd a6,  8 * 15(sp)
    sd a7,  8 * 16(sp)
    sd s2,  8 * 17(sp)
    sd s3,  8 * 18(sp)
    sd s4,  8 * 19(sp)
    sd s5,  8 * 20(sp)
    sd s6,  8 * 21(sp)
    sd s7,  8 * 22(sp)
    sd s8,  8 * 23(sp)
    sd s9,  8 * 24(sp)
    sd s10, 8 * 25(sp)
    sd s11, 8 * 26(sp)
    sd t3,  8 * 27(sp)
    sd t4,  8 * 28(sp)
    sd t5,  8 * 29(sp)
    sd t6,  8 * 30(sp)
    // 這 5 行是在把 CSR（控制狀態暫存器）的內容讀出來，先放到一般暫存器裡，等一下再 sd 存進 stack 的 context。
    csrr s0, sscratch
    csrr s1, sepc
    csrr s2, sstatus
    csrr s3, scause
    csrr s4, stval
    sd s0,  8 *  1(sp)
    sd s1,  8 * 31(sp)
    sd s2,  8 * 32(sp)
    sd s3,  8 * 33(sp)
    sd s4,  8 * 34(sp)

    // 不管是 u->s 還是 s->s 的 trap, sscratch 都不用保留 kernel 的 stack pointer 了
    csrw sscratch, zero
    mv a0, sp // a0 = sp (把 sp 這個暫存器的值 複製到 a0)
    // a0 是要給下一行 call do_trap 的 C 函式看
    // RISC-V 的呼叫慣例規定：函式第 1 個參數放 a0
    call do_trap

.global ret_from_exception
ret_from_exception:
    // 這五行是在「要離開 trap 前」，判斷要回到哪個 mode，並決定要不要把 sscratch 重新設成 kernel stack 指標。
    ld s0,  8 * 32(sp) // 從保存的 context 取出 sstatus（你在 save 時放在 slot 32）
    andi s0, s0, (1 << 8) // 取出 sstatus 的 bit 8，也就是 SPP
    bnez s0, 1f // 如果 bit 8 不為 0（SPP=1），代表要回 S-mode，直接跳到標號 1，不改 sscratch

    addi s0, sp, 8 * 35 // 如果沒有跳（SPP=0），代表要回 U-mode
    csrw sscratch, s0 // 把這個 kernel stack 位址放回 sscratch
1:
    ld s1,  8 * 31(sp)
    ld s2,  8 * 32(sp)
    csrw sepc, s1
    csrw sstatus, s2
    ld ra,  8 *  0(sp)
    ld gp,  8 *  2(sp)
    ld tp,  8 *  3(sp)
    ld t0,  8 *  4(sp)
    ld t1,  8 *  5(sp)
    ld t2,  8 *  6(sp)
    ld s0,  8 *  7(sp)
    ld s1,  8 *  8(sp)
    ld a0,  8 *  9(sp)
    ld a1,  8 * 10(sp)
    ld a2,  8 * 11(sp)
    ld a3,  8 * 12(sp)
    ld a4,  8 * 13(sp)
    ld a5,  8 * 14(sp)
    ld a6,  8 * 15(sp)
    ld a7,  8 * 16(sp)
    ld s2,  8 * 17(sp)
    ld s3,  8 * 18(sp)
    ld s4,  8 * 19(sp)
    ld s5,  8 * 20(sp)
    ld s6,  8 * 21(sp)
    ld s7,  8 * 22(sp)
    ld s8,  8 * 23(sp)
    ld s9,  8 * 24(sp)
    ld s10, 8 * 25(sp)
    ld s11, 8 * 26(sp)
    ld t3,  8 * 27(sp)
    ld t4,  8 * 28(sp)
    ld t5,  8 * 29(sp)
    ld t6,  8 * 30(sp)
    ld sp,  8 *  1(sp)
    sret // S-mode 回到 U-mode

.global switch_to
switch_to:
    // 1. Save Context    
    // 存 return address (書籤)
    sd ra, 0(a0)

    // 存 stack pointer (堆疊指標)
    sd sp, 8(a0)

    // 存 Callee-saved registers (s0 ~ s11)
    sd s0, 16(a0)
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    // 2. Restore Context
    // 讀 return address (書籤)
    ld ra, 0(a1)

    // 讀 stack pointer (堆疊指標)
    ld sp, 8(a1)

    // 讀 Callee-saved registers (s0 ~ s11)
    ld s0, 16(a1)
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)


    // 換發識別證：把 next 的位址(a1) 放進 tp (Thread Pointer) 暫存器
    // a0, a1 就是 main.c switch_to(prev, next) 的 prev, next 參數
    mv tp, a1 
    
    // 這邊 ret 會去找 ra 的值，跳到那邊去執行對應的程式
    ret