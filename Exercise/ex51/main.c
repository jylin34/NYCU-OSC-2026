extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);
extern void* kmalloc(unsigned long size);
extern void* alloc_page();

#define STACK_SIZE 0x1000

enum task_state {
    TASK_RUNNING,
    TASK_RUNNABLE,
    TASK_ZOMBIE
};

struct task_struct {
    struct thread_struct {
        unsigned long ra;
        unsigned long sp;
        unsigned long s[12];
    } thread;
    int pid;
    enum task_state state;
    unsigned long kernel_sp;
    unsigned long user_sp;
    unsigned long stack;
    struct task_struct* next;
};

static int nr_threads = 0;
static struct task_struct* run_queue = 0;
static struct task_struct* zombie_queue = 0;

struct task_struct* kthread_create(void (*threadfn)()) {
    struct task_struct* task = kmalloc(sizeof(struct task_struct));
    task->pid = nr_threads++;
    task->stack = (unsigned long)alloc_page();
    task->thread.ra = (unsigned long)threadfn;
    task->thread.sp = task->stack + STACK_SIZE;
    enqueue(&run_queue, task);
    return task;
}

void thread_exit() {
    struct task_struct* prev = get_current();

    current->state = TASK_ZOMBIE;

    enqueue(&zombie_queue, current);

    schedule();
}

static void enqueue(struct task_struct** queue, struct task_struct* task) {
    if (*queue == 0) {
        *queue = task;
        task->next = task;
    } else {
        struct task_struct* tail = (*queue)->next;
        (*queue)->next = task;
        task->next = tail;
    }
}

struct task_struct* get_current() {
    register struct task_struct* current asm("tp");
    return current;
}

extern void switch_to(struct task_struct* prev, struct task_struct* next);

void schedule() {
    struct task_struct* prev = get_current();
    struct task_struct* next;

    next = run_queue;
    run_queue = run_queue->next;

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_RUNNABLE;
        enqueue(&run_queue, prev);
    }

    next->state = TASK_RUNNING;

    if (prev != next) {
        switch_to(prev, next);
    }
}

void kill_zombies() {
    // TODO
}

void idle() {
    while (1) {
        kill_zombies();
        schedule();
    }
}

void foo() {
    for (int i = 0; i < 5; i++) {
        uart_puts("Process ID: ");
        uart_hex(get_current()->pid);
        uart_puts(" ");
        uart_hex(i);
        uart_puts("\n");
        for (int i = 0; i < 100000000; i++)
            ;
        schedule();
    }
    while (1)
        ;
}

void start_kernel() {
    uart_puts("\nStarting kernel ...\n");
    /* Initialize the thread pointer */
    asm volatile("move tp, %0" : : "r"(kthread_create(idle)));
    for (int i = 0; i < 3; i++)
        kthread_create(foo);
    idle();
}

void do_trap() {
    uart_puts("Kernel panic - do_trap\n");
    while (1)
        ;
}
