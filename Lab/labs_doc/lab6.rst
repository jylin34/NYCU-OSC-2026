.. warning::

   This document is currently under construction and may be incomplete or subject to significant changes.
   Please check back later for updates, and consult the instructor if you are unsure about any missing parts.

=====================
Lab 6: Virtual Memory
=====================

############
Introduction
############

Virtual memory provides isolated address spaces, 
so each user process can run in its address space without interfering with others.

In this lab, you need to initialize the memory management unit (MMU) and 
set up the address spaces for the kernel and user processes to achieve process isolation.

#################
Goals of this lab
#################

* Understand RISC-V Sv39 virtual memory system architecture.
* Understand how the kernel manages memory for user processes.
* Understand how demand paging works.
* Understand how copy-on-write works.

##########
Background
##########

Terminology
===========

Translation Levels
------------------

Translating a virtual address to a physical address involves levels of translation.
RISC-V Sv39 uses a three-level page table translation.

The top-level is page global directory (PGD), followed by page middle directory (PMD), and page table entry (PTE).

Page vs. Page Frame vs. Page Table
----------------------------------

**Page**: A chunk of virtual memory pointed to by one entry of a page table.

**Page frame**: A chunk of physical memory.

**Page table**: A page frame whose entries point to the next level page tables or pages.
In this documentation, PGD, PMD, and PTE are all called page tables.

Page Entry Descriptor
=====================

Each page table entry (PTE) contains the physical page number and flags describing access permissions and status.

We list the necessary content for you.

Descriptor's Format (simplified)
--------------------------------

.. code:: none

  63               10 9 8 7 6 5 4 3 2 1 0
  +----------------+---+---------------+
  |  PPN[2:0]      |...| flags         |
  +----------------+---+---------------+

  PPN: Physical Page Number (combined from fields across bits 10-53).
  Flags:
    V: valid
    R: readable
    W: writable
    X: executable
    U: user accessible
    G: global
    A: accessed
    D: dirty

Attributes Used in this Lab
---------------------------

**Bit[0] V (Valid)**  
  Indicates the entry is valid.

**Bit[1] R (Readable)**  
  Page is readable.

**Bit[2] W (Writable)**  
  Page is writable.

**Bit[3] X (Executable)**  
  Page is executable.

**Bit[4] U (User)**  
  1 for user mode accessible, 0 for kernel only.

**Bit[5] G (Global)**  
  1 if mapping is global.

**Bit[6] A (Accessed)**  
  Set by hardware when the page is accessed.

**Bit[7] D (Dirty)**  
  Set by hardware when the page is written.

RISC-V Sv39 Memory Layout
=========================

In the 39-bit virtual address space of Sv39, the upper address space is usually for kernel mode, and the lower address space is for user mode.

.. image:: /images/mem_layout_riscv.png

.. note::
  The entire accessible physical address could be linearly mapped to offset 0xffff_ffc0_0000_0000 for kernel access in this lab.
  It simplifies the design.

Configuration
=============

RISC-V Sv39 uses 3-level page table with 4KB page size and 512 entries per table.

To keep everything simple, the following configuration is specified for this lab:

* Paging mode: Sv39
* Page size: 4KB
* Virtual address space: 39-bit
* Physical memory access via linear mapping in kernel space
* No ASID support

.. image:: /images/lab6_sv39.jpg

Reference
=========

So far, we have briefly introduced the concept of virtual memory and RISC-V Sv39 virtual memory system architecture.
For details, you can refer to:

* `The RISC-V Instruction Set Manual: Volume II - Privileged Architecture(Version 20250724: Intermediate Release) <https://github.com/riscv/riscv-isa-manual/releases/download/riscv-isa-release-853e233-2025-07-24/riscv-privileged.pdf>`_
* **12.4. Sv39: Page-Based 39-bit Virtual-Memory System** of the RISC-V privileged architecture manual.

###############
Basic Exercises
###############

Basic Exercise 1 - Virtual Memory in Kernel Space - 10%
=======================================================

We provide a step-by-step tutorial to guide you to make your original kernel work with virtual memory.
However, we only give the essential explanation in each step.
For details, please refer to the manual.

SATP Register
-------------

Paging is enabled by writing to the `satp` register.

The following configuration is used in this lab:

.. code:: c

  #define SATP_SV39    (8L << 60)
  #define SATP_MODE_MASK (0xFULL << 60)
  #define MAKE_SATP(pagetable) (SATP_SV39 | ((((uint64_t)pagetable) >> 12) & 0xFFFFFFFFFFF))

  uint64_t satp_val = MAKE_SATP(pgd);
  asm volatile("csrw satp, %0" : : "r"(satp_val));
  asm volatile("sfence.vma");

.. admonition:: Todo

  Set up `satp` to enable virtual memory.

Memory Attributes
-----------------

In RISC-V, memory attributes like cacheability and permissions are managed through PTE bits and not separate attribute tables.

Use the following for this lab:

* Kernel mapping: readable, writable, executable, valid
* MMIO mapping: readable, writable, valid (not executable)
* User mapping: readable, writable, valid, user

Identity Mapping
----------------

Before enabling the MMU, you need to set up the page tables for the kernel.
Start with identity mapping using 2MB pages.

Each entry of PMD (second level) points to a 2MB block. 
Hence, you only need:

* One PGD
* One PMD (512 entries, each 2MB, for 1GB mapping)

**Setup**

* Allocate two pages: one for PGD, one for PMD
* Each PGD entry points to a PMD
* Each PMD entry maps 2MB of physical memory

.. code:: c

  #define PTE_V (1L << 0)
  #define PTE_R (1L << 1)
  #define PTE_W (1L << 2)
  #define PTE_X (1L << 3)
  #define PTE_U (1L << 4)
  #define PTE_G (1L << 5)
  #define PTE_A (1L << 6)
  #define PTE_D (1L << 7)

  uint64_t *pgd = alloc_page();
  uint64_t *pmd = alloc_page();

  pgd[0] = ((uint64_t)pmd >> 12 << 10) | PTE_V;

  for (int i = 0; i < 512; i++) {
    uint64_t pa = i * 0x200000;
    pmd[i] = (pa >> 12 << 10) | PTE_R | PTE_W | PTE_X | PTE_V | PTE_G;
  }

  uint64_t satp_val = MAKE_SATP(pgd);
  asm volatile("csrw satp, %0" : : "r"(satp_val));
  asm volatile("sfence.vma");

.. admonition:: Todo

  Set up identity mapping and enable MMU.

Map the Kernel Space
--------------------

You should map the kernel to the upper half of the virtual address space, e.g., starting from `0xffffffffc0000000`.

Modify your linker script:

.. code:: none

  SECTIONS
  {
    . = 0xffffffffc0000000;
    _kernel_start = .;
    ...
  }

You should create page table entries mapping the kernel's physical memory to this virtual region.

.. admonition:: Todo

  Modify linker script and create kernel-space mappings.

.. note::

  Hard-coded addresses such as MMIO addresses should also be mapped in the upper address space.

Finer Granularity Paging
------------------------

Use 4KB pages for regions where fine-grained protection is needed (e.g., user stack, .bss).
RISC-V supports 3-level paging: 4KB pages via PTE entries.

Map normal memory with readable/writable/executable bits as needed.
Map MMIO regions as non-executable and use `volatile` accesses to avoid speculative load.

.. admonition:: Todo

  Use 3-level mapping with finer granularity to distinguish MMIO and RAM.

Basic Exercise 2 - Virtual Memory in User Space - 30%
=====================================================

PGD Allocation
--------------

To isolate user processes, you should allocate a separate PGD for each user process.

Map the User Space
------------------

For mapping user memory, walk through 3-level page tables:

PGD → PMD → PTE

Allocate intermediate tables as needed. Here is a simplified walk function:

.. code-block:: c

  pte_t *walk(pagetable_t pagetable, uint64_t va, int alloc) {
    for (int level = 2; level > 0; level--) {
      pte_t *pte = &pagetable[PX(level, va)];
      if (*pte & PTE_V) {
        pagetable = (pagetable_t)PTE2PA(*pte);
      } else {
        if (!alloc) return 0;
        pagetable = alloc_page();
        memset(pagetable, 0, PAGE_SIZE);
        *pte = PA2PTE(pagetable) | PTE_V;
      }
    }
    return &pagetable[PX(0, va)];
  }

.. admonition:: Todo

  Implement function like ``mappages(pagetable pagetable, uint64_t va, uint64_t size, uint64_t pa, ...)`` and use it to map user code at 0x0 and user stack at 0xfffffffff000.

.. note::

  User space uses 4KB pages in this lab, requiring PGD, PMD, and PTE.

Revisit Syscalls
^^^^^^^^^^^^^^^^

You can now allow user programs to share the same virtual addresses, using per-process mappings.

Reimplement `fork()`, `exec()`, and system calls like `mbox_call` to use virtual memory properly.

.. admonition:: Todo

  Revisit syscalls to support address isolation via virtual memory.

Context Switch
--------------

To switch address space, write the process’s PGD to the `satp` register and flush TLB.

.. code:: c

  uint64_t satp_val = MAKE_SATP(next_pgd);
  asm volatile("csrw satp, %0" : : "r"(satp_val));
  asm volatile("sfence.vma");

.. admonition:: Todo

  Implement address space switch using `satp` and `sfence.vma`.

Video Player - 40%
==================

In order to test the correctness of your previous implementation, we provide a :download:`user program <vm.img>` that runs only if your kernel behaves as expected.

.. admonition:: Note

   The user program uses all syscalls from the previous lab.

.. warning::

  Only if you can run our test program fluently will you receive all the points; otherwise, even though you implemented the system call correctly, you will receive no points in this section.

##################
Advanced Exercises
##################

Advanced Exercise 1 - Mmap - 10%
================================

``mmap()`` is a system call to create memory regions for a user process.
Each region can be mapped to a file or anonymous pages (i.e., page frames not related to any file) with different protection.
Users can create heap and memory-mapped regions using this system call.

The kernel can also use it to implement the program loader.
Memory regions such as .text and .data can be created by **memory-mapped files**.
Regions like **.bss** and **user stack** can be created by **anonymous page mapping**.

.. admonition:: Note

   Because this lab does not use ELF files or actual files, you only need to implement anonymous page mapping.

API Specification
-----------------

(void*) mmap(void* addr, size_t len, int prot, int flags, int fd, int file_offset)

* If `addr` is NULL, the kernel chooses the start address.
* If `addr` is not NULL:
  * If the region overlaps with existing ones or is not page-aligned, treat `addr` as a hint.
  * Otherwise, use `addr` as the base of the new region.

* `len` must be page-aligned (rounded up if not).
* `prot` specifies access:
  * PROT_NONE: 0, inaccessible
  * PROT_READ: 1, readable
  * PROT_WRITE: 2, writable
  * PROT_EXEC: 4, executable

* `flags` include:
  * MAP_ANONYMOUS: Create anonymous pages (used for stack/heap)
  * MAP_POPULATE: Allocate physical pages immediately (optional if implementing demand paging)

Region Page Mapping
-------------------

If the user specifies MAP_POPULATE, the kernel should map physical pages immediately.

* For anonymous pages:
  1. Allocate page frames.
  2. Map the region to the allocated frames using the requested protection bits.

.. admonition:: Todo

  Implement the `mmap` syscall. Syscall number: 10.

Advanced Exercise 2 - Page Fault Handler & Demand Paging - 10%
==============================================================

So far, page frames have been pre-allocated.
But a user program may reserve large address spaces (e.g., heap, mmap) and not use all of them.
Pre-allocating wastes CPU time and memory.

Instead, allocate page frames **on demand**.

At process creation, only PGD is allocated.
When a page fault occurs:

* If the fault address is not in any mapped region:
  * Generate segmentation fault and terminate the process.
* If the fault address is in a valid region:
  * Allocate a page frame and map only that page.

.. admonition:: Note

  To verify correctness, log each fault:

.. code-block:: c

  // Translation fault
  printf("[Translation fault]: %lx\n", addr);
  // Segmentation fault
  printf("[Segmentation fault]: Kill Process\n");

.. admonition:: Todo

  Implement page fault handler for demand paging.

Advanced Exercise 3 - Copy on Write - 10%
=========================================

In your previous fork implementation, the kernel copies all page frames for the child.
But `exec()` usually follows `fork()`, meaning those frames may never be used.

To optimize this, implement **copy-on-write (COW)**.

On Fork a New Process
---------------------

1. Copy the page tables (PGD, etc.).
2. Mark all user PTE entries **read-only**, even if they were originally read-write.
3. Increment reference counts for each shared page frame.

On Page Write by Either Process
-------------------------------

If a process writes to a read-only page, a **permission fault** occurs.
Then:

* If the region is marked read-only:
  * Segmentation fault.
* If the region is writable:
  * It's a copy-on-write fault:
    * Allocate a new frame
    * Copy the data
    * Update PTE to be writable and point to the new frame
    * Update reference count

.. note::

  Track reference counts per frame to determine when to free memory.

.. admonition:: Todo

  Implement copy-on-write mechanism.

.. note::

  If your user program accesses any memory-mapped I/O regions for inter-process communication, ensure these regions are mapped directly in each process without copy-on-write, to maintain correct behavior.