extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);

/* Memory map */
#define PAGE_OFFSET   0xffffffc000000000UL
#define PAGE_SIZE     (1UL << 12) // 4KB
#define PMD_SIZE      (1UL << 21) 
#define PGD_SIZE      (1UL << 30)

/* VA bit-field shifts (Sv39) */
#define PGD_SHIFT     30 
#define PMD_SHIFT     21
#define PTE_SHIFT     12

#define ENTRIES_PER_TABLE  512

#define KERNEL_PGD_INDEX   ((PAGE_OFFSET >> PGD_SHIFT) & 0x1FF)

#define LINEAR_MAP_GIB     4

/* PTE descriptor bits (Sv39) */
#define PTE_V  (1UL << 0)  
#define PTE_R  (1UL << 1)
#define PTE_W  (1UL << 2)
#define PTE_X  (1UL << 3)
#define PTE_U  (1UL << 4)
#define PTE_G  (1UL << 5)
#define PTE_A  (1UL << 6)
#define PTE_D  (1UL << 7)

#define PROT_KERNEL  (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)

#define SATP_SV39           (8UL << 60)
#define MAKE_SATP(pgd_pa)   (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))

#define MAKE_PTE(pa, flags) ((((unsigned long)(pa)) >> 12) << 10 | (flags))


static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pgd[ENTRIES_PER_TABLE] = { 0 };

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pmd[LINEAR_MAP_GIB][ENTRIES_PER_TABLE] = { { 0 } };

void setup_vm(void)
{
    unsigned long pa;
    unsigned long low_pgd_index = (0x80000000UL >> PGD_SHIFT) & 0x1FF;
    unsigned long kernel_pgd_index_base = ((PAGE_OFFSET + 0x080000000UL) >> PGD_SHIFT) & 0x1FF;

    /* clear page tables */
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        pgd[i] = 0;
    }
    for (int g = 0; g < LINEAR_MAP_GIB; g++)
        for (int i = 0; i < ENTRIES_PER_TABLE; i++)
            pmd[g][i] = 0;

    /* Identity + higher-half kernel mapping: map 4 GiB as 1 GiB pages */
    for (int i = 0; i < LINEAR_MAP_GIB; i++) {
        pa = 0x80000000UL + (unsigned long)i * PGD_SIZE;
        /* identity mapping (low VA == PA) */
        pgd[low_pgd_index + i] = MAKE_PTE(pa, PROT_KERNEL);

        /* higher-half mapping: place identical PA under high VA region */
        pgd[kernel_pgd_index_base + i] = MAKE_PTE(pa, PROT_KERNEL);
    }

    /* Map UART MMIO: PA 0x10000000 -> VA PAGE_OFFSET + 0x01000000
     * Use a second-level PMD table for this 1 GiB PGD slot and make a 2MiB leaf.
     */
    unsigned long uart_pa = 0x10000000UL;
    unsigned long uart_va = PAGE_OFFSET + 0x01000000UL;
    unsigned long uart_pgd_idx = (uart_va >> PGD_SHIFT) & 0x1FF;
    unsigned long uart_pmd_idx = (uart_va >> PMD_SHIFT) & 0x1FF;
    unsigned long pmd_slot = (uart_pgd_idx - ((PAGE_OFFSET >> PGD_SHIFT) & 0x1FF));

    /* install PMD page table in the chosen PGD slot (non-leaf PTE should have V=1 only) */
    pgd[uart_pgd_idx] = MAKE_PTE((unsigned long)&pmd[pmd_slot] - PAGE_OFFSET, PTE_V);

    /* create a 2MiB leaf entry in that PMD for UART */
    pmd[pmd_slot][uart_pmd_idx] = MAKE_PTE(uart_pa, PROT_KERNEL);

    /* enable satp with physical address of pgd */
    unsigned long pgd_pa = (unsigned long)pgd - PAGE_OFFSET;
    unsigned long satp = MAKE_SATP(pgd_pa);
    uart_puts("\n[Kernel] Before turn on satp ...\n");
    // printf("\n[Kernel Print] Before turn on satp ... satp: %l\n", satp);
    asm volatile("csrw satp, %0" : : "r"(satp) : "memory");
    asm volatile("sfence.vma" : : : "memory");
    uart_puts("[Kernel] After turn on satp ...\n");
}

void drop_identity_map(void)
{
    unsigned long low_pgd_index = (0x80000000UL >> PGD_SHIFT) & 0x1FF;
    /* clear the PGD entries used for the low identity mapping */
    for (int i = 0; i < LINEAR_MAP_GIB; i++) {
        pgd[low_pgd_index + i] = 0;
    }
    asm volatile("sfence.vma" : : : "memory");
}

void start_kernel(void)
{
    uart_puts("\nStarting kernel at : ");
    uart_hex((unsigned long)start_kernel);
    uart_puts("\n");
    while (1) {
        uart_putc(uart_getc());
    }
}