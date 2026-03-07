.section ".text.boot"
.global _start
_start:
// TODO: Clear the .bss section
    la a3, __bss_start
    la a4, __bss_stop
    ble a4, a3, clear_bss_done
clear_bss:
    sd zero, (a3)
    add a3, a3, 8
    blt a3, a4, clear_bss
    
// TODO: Set up the stack pointer     
clear_bss_done:
    la sp, _end
    tail start_kernel
