#include "kernel.hh"
#include "k-apic.hh"
#include "k-vmiter.hh"
#include "obj/k-firstprocess.h"

// kernel.cc
//
//    This is the kernel.


// INITIAL PHYSICAL MEMORY LAYOUT
//
//  +-------------- Base Memory --------------+
//  v                                         v
// +-----+--------------------+----------------+--------------------+---------/
// |     | Kernel      Kernel |       :    I/O | App 1        App 1 | App 2
// |     | Code + Data  Stack |  ...  : Memory | Code + Data  Stack | Code ...
// +-----+--------------------+----------------+--------------------+---------/
// 0  0x40000              0x80000 0xA0000 0x100000             0x140000
//                                             ^
//                                             | \___ PROC_SIZE ___/
//                                      PROC_START_ADDR

#define PROC_SIZE 0x40000       // initial state only

proc ptable[NPROC];             // array of process descriptors
                                // Note that `ptable[0]` is never used.
proc* current;                  // pointer to currently executing proc

bool show_memory = false;       // whether to show memory


// Memory state - see `kernel.hh`
physpageinfo physpages[NPAGES];


[[noreturn]] void schedule();
[[noreturn]] void run(proc* p);
void exception(regstate* regs);
uintptr_t syscall(regstate* regs);
void memshow();


// kernel_start(command)
//    Initialize the hardware and processes and start running. The `command`
//    string is an optional string passed from the boot loader.

static void process_setup(pid_t pid, const char* program_name);

void kernel_start(const char* command) {
    // initialize hardware
    init_hardware();
    log_printf("Starting WeensyOS\n");

    ticks = 1;
    init_timer(HZ);

    // clear screen
    console_clear();

    // (re-)initialize kernel page table
    for (vmiter it(kernel_pagetable, 0);
         it.va() < MEMSIZE_PHYSICAL;
         it += PAGESIZE) {
        if (it.va() == CONSOLE_ADDR) {
            it.map(it.va(), PTE_P | PTE_W | PTE_U);
        } else if (it.va() != 0) {
            it.map(it.va(), PTE_P | PTE_W);
        } else {
            // nullptr is inaccessible even to the kernel
            it.map(it.va(), 0);
        }
    }

    // set up process descriptors and run first processes
    for (pid_t i = 0; i < NPROC; i++) {
        ptable[i].pid = i;
        ptable[i].state = P_FREE;
    }
    if (!command) {
        command = WEENSYOS_FIRST_PROCESS;
    }
    if (!program_image(command).empty()) {
        process_setup(1, command);
    } else if (strcmp(command, "pipe") == 0) {
        process_setup(1, "pipewriter");
        process_setup(2, "pipereader");
    } else {
        process_setup(1, "alice");
        process_setup(2, "eve");
    }
    run(&ptable[1]);
}


// kalloc(sz)
//    Kernel memory allocator. Allocates `sz` contiguous bytes and
//    returns a pointer to the allocated memory, or `nullptr` on failure.
//
//    The returned memory is initialized to 0xCC, which corresponds to
//    the x86 instruction `int3` (this may help you debug). You'll
//    probably want to reset it to something more useful.
//
//    On WeensyOS, `kalloc` is a page-based allocator: if `sz > PAGESIZE`
//    the allocation fails; if `sz < PAGESIZE` it allocates a whole page
//    anyway.
//
//    The handout code returns the next allocatable free page it can find.
//    It never reuses pages or supports freeing memory (you'll change that).

static uintptr_t next_alloc_pa;

void* kalloc(size_t sz) {
    if (sz > PAGESIZE) {
        return nullptr;
    }

    while (next_alloc_pa < MEMSIZE_PHYSICAL) {
        uintptr_t pa = next_alloc_pa;
        next_alloc_pa += PAGESIZE;

        if (allocatable_physical_address(pa)
            && physpages[pa / PAGESIZE].refcount == 0) {
            ++physpages[pa / PAGESIZE].refcount;
            memset((void*) pa, 0xCC, PAGESIZE);
            return (void*) pa;
        }
    }
    return nullptr;
}


// kfree(kptr)
//    Free `kptr`, which must have been previously returned by `kalloc`.
//    If `kptr == nullptr` does nothing.

void kfree(void* kptr) {
    (void) kptr;
    assert(false /* your code here */);
}


// process_setup(pid, program_name)
//    Load application program `program_name` as process number `pid`.
//    This loads the application's code and data into memory, sets its
//    %rip and %rsp, gives it a stack page, and marks it as runnable.

void process_setup(pid_t pid, const char* program_name) {
    init_process(&ptable[pid], 0);

    // We expect all process memory to reside between
    // first_addr and last_addr.
    uintptr_t first_addr = PROC_START_ADDR + (pid - 1) * PROC_SIZE;
    uintptr_t last_addr = PROC_START_ADDR + pid * PROC_SIZE;

    // initialize process page table
    ptable[pid].pagetable = kernel_pagetable;

    // obtain reference to the program image
    program_image pgm(program_name);

    // allocate and map all memory
    for (auto seg = pgm.begin(); seg != pgm.end(); ++seg) {
        for (uintptr_t a = round_down(seg.va(), PAGESIZE);
             a < seg.va() + seg.size();
             a += PAGESIZE) {
            assert(a >= first_addr && a < last_addr);
            assert(physpages[a / PAGESIZE].refcount == 0);
            ++physpages[a / PAGESIZE].refcount;
            vmiter(ptable[pid].pagetable, a).map(a, PTE_P | PTE_W | PTE_U);
        }
    }

    // copy instructions and data into place
    for (auto seg = pgm.begin(); seg != pgm.end(); ++seg) {
        memset((void*) seg.va(), 0, seg.size());
        memcpy((void*) seg.va(), seg.data(), seg.data_size());
    }

    // mark entry point
    ptable[pid].regs.reg_rip = pgm.entry();

    // allocate stack
    uintptr_t stack_addr = last_addr - PAGESIZE;
    assert(physpages[stack_addr / PAGESIZE].refcount == 0);
    ++physpages[stack_addr / PAGESIZE].refcount;
    vmiter(ptable[pid].pagetable, stack_addr).map(stack_addr, PTE_P | PTE_W | PTE_U);
    ptable[pid].regs.reg_rsp = stack_addr + PAGESIZE;

    // mark process as runnable
    ptable[pid].state = P_RUNNABLE;
}



// exception(regs)
//    Exception handler (for interrupts, traps, and faults).
//
//    The register values from exception time are stored in `regs`.
//    The processor responds to an exception by saving application state on
//    the kernel's stack, then jumping to kernel assembly code (in
//    k-exception.S). That code saves more registers on the kernel's stack,
//    then calls exception().
//
//    Note that hardware interrupts are disabled when the kernel is running.

void exception(regstate* regs) {
    // Copy the saved registers into the `current` process descriptor.
    current->regs = *regs;
    regs = &current->regs;

    // It can be useful to log events using `log_printf`.
    // Events logged this way are stored in the host's `log.txt` file.
    //log_printf("proc %d: exception %d at rip %p\n",
    //           current->pid, regs->reg_intno, regs->reg_rip);

    // Show the current cursor location and memory state
    // (unless this is a kernel fault).
    console_show_cursor(cursorpos);
    if (regs->reg_intno != INT_PF || (regs->reg_errcode & PTE_U)) {
        memshow();
    }

    // If Control-C was typed, exit the virtual machine.
    check_keyboard();


    // Actually handle the exception.
    switch (regs->reg_intno) {

    case INT_IRQ + IRQ_TIMER:
        ++ticks;
        lapicstate::get().ack();
        schedule();
        break;                  /* will not be reached */

    case INT_PF: {
        // Analyze faulting address and access type.
        uintptr_t addr = rdcr2();
        const char* operation = regs->reg_errcode & PTE_W
                ? "write" : "read";
        const char* problem = regs->reg_errcode & PTE_P
                ? "protection problem" : "missing page";

        if (!(regs->reg_errcode & PTE_U)) {
            panic("Kernel page fault on %p (%s %s, rip=%p)!\n",
                  addr, operation, problem, regs->reg_rip);
        }
        error_printf(CPOS(24, 0), 0x0C00,
                     "Process %d page fault on %p (%s %s, rip=%p)!\n",
                     current->pid, addr, operation, problem, regs->reg_rip);
        current->state = P_FAULTED;
        break;
    }

    default:
        panic("Unexpected exception %d!\n", regs->reg_intno);

    }


    // Return to the current process (or run something else).
    if (current->state == P_RUNNABLE) {
        run(current);
    } else {
        schedule();
    }
}


// syscall(regs)
//    System call handler.
//
//    The register values from system call time are stored in `regs`.
//    The return value, if any, is returned to the user process in `%rax`.
//
//    Note that hardware interrupts are disabled when the kernel is running.

int syscall_page_alloc(uintptr_t addr);
pid_t syscall_spawn(const char* command);
ssize_t syscall_pipewrite(const char* buf, size_t sz);
ssize_t syscall_piperead(char* buf, size_t sz);

uintptr_t syscall(regstate* regs) {
    // Copy the saved registers into the `current` process descriptor.
    current->regs = *regs;
    regs = &current->regs;

    // It can be useful to log events using `log_printf`.
    // Events logged this way are stored in the host's `log.txt` file.
    //log_printf("proc %d: syscall %d at rip %p\n",
    //           current->pid, regs->reg_rax, regs->reg_rip);

    // Show the current cursor location and memory state
    console_show_cursor(cursorpos);
    memshow();

    // If Control-C was typed, exit the virtual machine.
    check_keyboard();


    // Actually handle the exception.
    switch (regs->reg_rax) {

    case SYSCALL_PANIC:
        user_panic(current);    // does not return

    case SYSCALL_GETPID:
        return current->pid;

    case SYSCALL_YIELD:
        current->regs.reg_rax = 0;
        schedule();             // does not return

    case SYSCALL_PAGE_ALLOC:
        return syscall_page_alloc(regs->reg_rdi);

    case SYSCALL_GETSYSNAME: {
        const char* osname = "DemoOS 61.61";
        char* buf = (char*) current->regs.reg_rdi;
        strcpy(buf, osname);
        return 0;
    }

    case SYSCALL_SPAWN:
        return syscall_spawn((const char*) regs->reg_rdi);

    case SYSCALL_PIPEWRITE:
        return syscall_pipewrite((const char*) regs->reg_rdi, regs->reg_rsi);

    case SYSCALL_PIPEREAD:
        return syscall_piperead((char*) regs->reg_rdi, regs->reg_rsi);

    default:
        panic("Unexpected system call %ld!\n", regs->reg_rax);

    }

    panic("Should not get here!\n");
}


// syscall_page_alloc(addr)
//    Handles the SYSCALL_PAGE_ALLOC system call. This function
//    should implement the specification for `sys_page_alloc`
//    in `u-lib.hh` (but in the handout code, it does not).

int syscall_page_alloc(uintptr_t addr) {
    assert(physpages[addr / PAGESIZE].refcount == 0);
    ++physpages[addr / PAGESIZE].refcount;
    memset((void*) addr, 0, PAGESIZE);
    return 0;
}


// syscall_spawn(command)
//    Handles the SYSCALL_SPAWN system call; see `sys_spawn` in `u-lib.hh`.

pid_t syscall_spawn(const char* command) {
    return -1;
}


// pipe buffer

char pipebuf[1];
size_t pipebuf_len = 0;

// syscall_pipewrite(buf, sz)
//    Handles the SYSCALL_PIPEWRITE system call; see `sys_pipewrite`
//    in `u-lib.hh`.

ssize_t syscall_pipewrite(const char* buf, size_t sz) {
    if (sz == 0) {
        // nothing to write
        return 0;
    } else if (pipebuf_len == 1) {
        // kernel buffer full, try again
        return -1;
    } else {
        // write one character
        pipebuf[0] = buf[0];
        pipebuf_len = 1;
        return 1;
    }
}

// syscall_piperead(buf, sz)
//    Handles the SYSCALL_PIPEREAD system call; see `sys_piperead`
//    in `u-lib.hh`.

ssize_t syscall_piperead(char* buf, size_t sz) {
    if (sz == 0) {
        // no room to read
        return 0;
    } else if (pipebuf_len == 0) {
        // kernel buffer empty, try again
        return -1;
    } else {
        // read one character
        buf[0] = pipebuf[0];
        pipebuf_len = 0;
        return 1;
    }
}


// schedule
//    Pick the next process to run and then run it.
//    If there are no runnable processes, spins forever.

void schedule() {
    pid_t pid = current->pid;
    for (unsigned spins = 1; true; ++spins) {
        pid = (pid + 1) % NPROC;
        if (ptable[pid].state == P_RUNNABLE) {
            run(&ptable[pid]);
        }

        // If Control-C was typed, exit the virtual machine.
        check_keyboard();

        // If spinning forever, show the memviewer.
        if (spins % (1 << 12) == 0) {
            memshow();
        }
    }
}


// run(p)
//    Run process `p`. This involves setting `current = p` and calling
//    `exception_return` to restore its page table and registers.

void run(proc* p) {
    assert(p->state == P_RUNNABLE);
    current = p;

    // Check the process's current pagetable.
    check_pagetable(p->pagetable);

    // This function is defined in k-exception.S. It restores the process's
    // registers then jumps back to user mode.
    exception_return(p);

    // should never get here
    while (true) {
    }
}


// memshow()
//    Draw a picture of memory (physical and virtual) on the CGA console.
//    Switches to a new process's virtual memory map every 0.25 sec.
//    Uses `console_memviewer()`, a function defined in `k-memviewer.cc`.

void memshow() {
    static unsigned long last_ticks = 0;
    static int showing = 0;

    if (show_memory) {
        // switch to a new process every 0.25 sec
        if (last_ticks == 0 || ticks - last_ticks >= HZ / 2) {
            last_ticks = ticks;
            showing = (showing + 1) % NPROC;
        }

        proc* p = nullptr;
        for (int search = 0; !p && search < NPROC; ++search) {
            if (ptable[showing].state != P_FREE
                && ptable[showing].pagetable) {
                p = &ptable[showing];
            } else {
                showing = (showing + 1) % NPROC;
            }
        }

        console_memviewer(p);
    }
}
