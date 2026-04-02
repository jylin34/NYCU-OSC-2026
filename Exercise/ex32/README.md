# Memory Reservation

## Exercise3 Slide
[OSC2026_lab3_exercise](<https://docs.google.com/presentation/d/1P4Egk-PbElJc_u8yhMWcbMo3Ec8ch0w5pQj-tCUqZWQ/edit?slide=id.g3bf45098e55_0_0#slide=id.g3bf45098e55_0_0>)

## Intro. 
This program incorporates physical contiguity checks and exclusion
logic for hardware reserved regions. By dynamically scanning and switching the
state flags of adjacent physical nodes, it prevents unusable or reserved memory
nodes from being enlisted into the free list arrays, ensuring precise control
over the physical locations of available memory blocks.

## Memory Reservation 
### `get_buddy`
- Employs bitwise logic and XOR operations to locate the designated "buddy" node
  of a target physical node.

### `memory_reserve`
- Evaluates physical addresses and evicts intersecting blocks.
    - **Boundary Calculation :** Translates base addresses and sizes via
      `PAGE_SIZE` into corresponding Page Frame Number (PFN) boundaries:
      `start_pfn` and `end_pfn`.
    - **Descending Order Loop :** Commencing from `MAX_ORDER`, it scans all
      existing contiguous physical blocks. It calculates the starting and ending
      PFN for each block.
    - **Exclusion and Split Decisions :** 
        - If the block's PFN is completely outside the reserved boundaries, it
          is bypassed, and the iterator proceeds to the next node.
        - Otherwise, the block is extracted from the current order's `free_area`
          array.
        - If the block falls entirely within the reserved range, its node is
          permanently marked as occupied, preventing it from being returned to
          the system pool.
        > [!IMPORTANT] If the block only **"partially overlaps"** with the
        > reserved boundary, it is split in half into two lower-order buddy
        > nodes. Both nodes are demoted and pushed into the lower-order
        > `free_area` array for re-evaluation in the subsequent loop iteration.
        

## Reserved Regions ( Implement in Lab3 )
- **DTB Blob :** use the `fdt_ptr` and `fdt_header.totalsize` to obtain start
  and size.
- **Kernel Image :** Obtain its physical memory boundaries via symbols defined
  in the `linker.ld` script.
- **Initramfs :** use /chosen properties linux,initrd-start and linux,initrd-end
  to obtain its range. (already done in Lab 2)
- Any additional reserved regions (if have, e.g., spin tables, DMA buffers,
  platform-specific). You may retrieve via /reserved-memory node in the device
  tree.


## TODO
- `memory_reserve`: Mark specific physical memory regions as occupied to prevent
  the Buddy System from allocating them

## Verification
We have provided a `Makefile` to automate the process of building the kernel and
running it within the QEMU emulator. 
```bash
make run
```

## Expected Result
```c
free_area[10] 2037
free_area[9] 0
free_area[8] 1
free_area[7] 1
free_area[6] 0
free_area[5] 0
free_area[4] 1
free_area[3] 0
free_area[2] 1
free_area[1] 1
free_area[0] 0
```
