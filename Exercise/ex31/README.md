# The Buddy Allocator

## Exercise3 Slide
[OSC2026_lab3_exercise](<https://docs.google.com/presentation/d/1P4Egk-PbElJc_u8yhMWcbMo3Ec8ch0w5pQj-tCUqZWQ/edit?slide=id.g3bf45098e55_0_0#slide=id.g3bf45098e55_0_0>)

## Intro. 
Implement the core memory management logic of a binary buddy system,
which involves splitting larger contiguous blocks upon allocation if appropriate
sizes are unavailable, merging physically adjacent free blocks back into larger
ones upon declassification, and managing the array of free list references.

## Buddy System
- Memory block sizes are strictly maintained as powers of two.
- If a block of the requested order is unavailable, locate a free block from the
  next higher order and split it into two equal-sized "buddies".
- The system checks if its physically adjacent buddy of the exact same size is
  free. If true, merge them immediately into a single continuous block of the
  next higher order.

### `alloc_pages`
- Executes top-down block searching and recursive splitting to allocate required
  memory pages.
- Implementation Requirements
    - **Space Search :** Starting from the requested `order`, traverse upward
      through the `free_area` array to locate the first list containing free
      nodes. If the traversal reaches `MAX_ORDER` and all lists are empty, the
      allocation fails.
    - **Block Splitting :** If the order of the located block is greater than
      the target `order`, divide the block in half and obtain the pointer of its
      buddy node at the reduced order , and push this buddy node into the
      `free_area` list of that reduced `order`.
    - **Status Update :** Remove the allocated page node from the `free_area`,
      update its internal `order` variable. 

### `free_pages`
- Executes page deallocation and bottom-up merging of adjacent physical blocks.
- Implementation Requirements
    - **Iterative Merging :** Initiate a loop. Calculate and retrieve the buddy
      node using `get_buddy` based on the current node and its `order`.
    - **Merge Condition Evaluation :** Verify if the buddy node satisfies two
      criteria: (a) It is in a free state; (b) Its order equals the current
      `order`. Terminate the merging process if either condition is unmet or if
      `MAX_ORDER` is reached.
    - **Pointer Aggregation :** If conditions are satisfied, remove the buddy
      node from its `free_area` list. Compare the memory addresses of the
      current node and the buddy node; retain the lower address as the base
      pointer of the newly merged block.
    - **Node Mounting :** Upon merge termination, push the final aggregated node
      into the `free_area` list corresponding to its final `order`.


## TODO
- `alloc_pages`: Allocate a contiguous block of pages using the split logic.
- `free_pages`: Release the specified page block and iteratively merge it with
  adjacent unallocated buddies.

## Verification We have provided a `Makefile` to automate the process of
building the kernel and running it within the QEMU emulator. 
```bash 
make run
```

## Expected Result
```c
p1:
free_area[10] 2559
free_area[9] 1
free_area[8] 1
free_area[7] 1
free_area[6] 1
free_area[5] 1
free_area[4] 1
free_area[3] 1
free_area[2] 1
free_area[1] 1
free_area[0] 0

p2:
free_area[10] 2559
free_area[9] 1
free_area[8] 1
free_area[7] 1
free_area[6] 1
free_area[5] 1
free_area[4] 1
free_area[3] 1
free_area[2] 1
free_area[1] 0
free_area[0] 0

p3:
free_area[10] 2559
free_area[9] 1
free_area[8] 1
free_area[7] 1
free_area[6] 1
free_area[5] 1
free_area[4] 1
free_area[3] 1
free_area[2] 0
free_area[1] 1
free_area[0] 0

free:
free_area[10] 2560
free_area[9] 0
free_area[8] 0
free_area[7] 0
free_area[6] 0
free_area[5] 0
free_area[4] 0
free_area[3] 0
free_area[2] 0
free_area[1] 0
free_area[0] 0
```
