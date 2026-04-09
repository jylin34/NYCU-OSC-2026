# PLIC & UART Interrupt Handling

## Exercise4 Slides

[OSC2026_lab4_exercise](<https://docs.google.com/presentation/d/1EjUb9UFXF-r-aVb-Vh7ITe6OMMV4Gj7kIVjWVzY_0us/edit?usp=sharing>)

## Introduction
In this exercise, we will dive into bare-metal interrupt handling on the RISC-V architecture. You will learn how to configure the **Platform-Level Interrupt Controller (PLIC)** to route external hardware interrupts and set up the **UART** to trigger interrupts upon receiving user input. 

The ultimate goal is to build a simple "Echo" kernel: whenever you type a character on your keyboard, the UART hardware triggers an interrupt, the PLIC routes it to the CPU, and the CPU reads the character and prints it back to the terminal.

## Learning Objectives
1. Understand how to initialize and configure the PLIC (`plic_init`).
2. Learn the hardware-software handshake process for interrupt handling (`plic_claim` & `plic_complete`).
3. Configure Memory-Mapped I/O (MMIO) registers to enable UART RX interrupts (`uart_init`).

## TODOs

You need to complete the missing code blocks marked with `// TODO` in the source file.

### 1. `plic_init()`
Set up the PLIC routing rules for the UART interrupt:
* **Priority:** Set the priority of `UART_IRQ` to `1` (greater than 0 to enable it).
* **Enable:** Enable the `UART_IRQ` for the boot hart.
* **Threshold:** Set the priority threshold of the boot hart to `0` (accepting all active interrupts).

### 2. `plic_claim()` & `plic_complete()`
Implement the standard interrupt claim/complete flow:
* **`plic_claim()`:** Read the PLIC Claim register to get the pending IRQ number.
* **`plic_complete(irq)`:** Write the processed IRQ number back to the PLIC Complete register to unlock the gateway for future interrupts.

### 3. `uart_init()`
Configure the UART hardware to generate interrupts:
* **Enable RX Interrupt:** Set Bit 0 of the `UART_IER` (Interrupt Enable Register) to `1`.
* **Enable Interrupt Output:** Set Bit 3 (OUT2) of the `UART_MCR` (Modem Control Register) to `1` to allow the interrupt signal to be routed to the PLIC.

## Verification

Build and run the kernel using the following command:

```bash
make run
```

## Expected Result
You should see what you type on the screen.

```text
Starting kernel ...
hello
osc

```
