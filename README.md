This is my Operating System Capstone (OSDI) course's repository took in 114-2 Spring semester.

A bare-metal OS kernel built from scratch in C for the **OrangePi RV2 (RISC-V)**, developed and validated primarily on **QEMU**. Each lab builds directly on the previous one — from a UART bootloader up through a Sv39 virtual-memory kernel with a VFS.

Code is intentionally rough in places (time-pressured coursework): later labs copy-forward and re-duplicate earlier drivers/parsers rather than sharing a common library.

### Labs

- **Lab 0 — Environment Setup**: Set up the RISC-V toolchain and QEMU, and produced the first bootable kernel image for the OrangePi RV2.
- **Lab 1 — Hello World**: Wrote a UART driver and a minimal interactive shell for bare-metal I/O.
- **Lab 2 — Booting**: Parsed the Devicetree (FDT) for hardware discovery and loaded an initial ramdisk (CPIO) via a UART bootloader.
- **Lab 3 — Memory Allocator**: Implemented a buddy allocator over physical memory regions discovered from the Device Tree.
- **Lab 4 — Exception and Interrupt**: Built the S/U-mode trap vector, register save/restore, and syscall dispatch for RISC-V exception handling.
- **Lab 5 — Thread and User Process**: Implemented kernel threads, context switching, a scheduler, and user-mode process execution (fork/exec).
- **Lab 6 — Virtual Memory**: Set up Sv39 paging with a higher-half kernel and per-process page tables for address-space isolation.
- **Lab 7 — Virtual File System**: Built a VFS layer with tmpfs and ramfs mounted underneath it.
