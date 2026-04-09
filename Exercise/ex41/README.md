# Exception Level Switch and Exception Handling

## Exercise4 Slides

[OSC2026_lab4_exercise](<https://docs.google.com/presentation/d/1EjUb9UFXF-r-aVb-Vh7ITe6OMMV4Gj7kIVjWVzY_0us/edit?usp=sharing>)

## Introduction

This exercise extends the previous initrd-based environment to support user program execution and trap handling. 

In `ex41`, the kernel boots, loads `prog.bin` from the initrd, and transfers control to the user program. While the user program is running in U-mode, the kernel must be fully equipped to handle traps, save the full context, process the exception, and return safely to user space.

## User Program Loading

### `exec`

The `exec` routine is responsible for locating `prog.bin` inside the initrd and preparing it for execution.

**Implementation points:**
- Parse the CPIO header fields with correct hexadecimal conversion.
- Compute the aligned size of the header and file payload.
- Match the requested file name against the archive entry names.
- Configure specific CSRs and execute the `sret` instruction to transition from S-mode and jump to the user program.

## Trap Frame

To handle traps safely without corrupting the running program, the kernel must save all general-purpose registers alongside the privileged state registers.

This state is typically stored in a `struct pt_regs` (Trap Frame), which should contain at least:
- **Integer registers:** `ra`, `gp`, `tp`, `t0`–`t6`, `s0`–`s11`, `a0`–`a7`
- **Saved supervisor registers:** `sepc`, `sstatus`, `scause`, `stval`

*Note: The layout of your C struct must exactly match the save/restore sequence implemented in your assembly code (e.g., `start.S`).*

## `do_trap`

`do_trap(struct pt_regs *regs)` is the main C-level trap handler. 

**Required behavior:**
- Print the saved `sepc` and `scause` values from the `regs` pointer.
- Advance the `sepc` value within the struct by `4`. If you do not advance the program counter, the hardware will continuously re-execute the trapping instruction upon returning to U-mode, resulting in an infinite loop.
- Return gracefully, allowing the assembly stub to restore the context from `regs` and execute `sret`.

## Verification

Build and run the kernel using the following command:

```bash
make run
```

## Expected Result

Upon successful execution, the kernel will boot and load `prog.bin` from the initrd. As `prog.bin` executes five `ecall` instructions, your kernel must successfully trap and output the `sepc` and `scause` registers five times accordingly.

**Typical output will look like this:**

```text
Starting kernel ...
sepc: 0x000000008820007a, scause: 0x0000000000000008
sepc: 0x000000008820007a, scause: 0x0000000000000008
sepc: 0x000000008820007a, scause: 0x0000000000000008
sepc: 0x000000008820007a, scause: 0x0000000000000008
sepc: 0x000000008820007a, scause: 0x0000000000000008
```
